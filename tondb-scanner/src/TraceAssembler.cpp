#include "TraceAssembler.h"
#include "convert-utils.h"
#include <map>
#include <functional>

//
// Utils
//
#define B64HASH(x) (td::base64_encode((x).as_slice()))

template<class T>
std::optional<T> to_std_optional(td::optional<T> item) {
    return (item ? std::optional<T>(item.value()) : std::optional<T>());
}

//
// Utils
//
std::string TraceEdgeImpl::str() const {
    td::StringBuilder sb;
    sb << "TraceEdge("
       << msg_hash << ", " 
       << (left_tx.has_value() ? B64HASH(left_tx.value()) : "null") << ", "
       << (right_tx.has_value() ? B64HASH(right_tx.value()) : "null") << ", "
       << (src.has_value() ? src.value() : "null") << ", "
       << (dest.has_value() ? dest.value() : "null") << ")";
    return sb.as_cslice().str();
}

std::string TraceImpl::str() const {
    td::StringBuilder sb;
    sb << "Trace(" << trace_id << ", edges="  << edges.size() << ", txs=" << txs.size() << ")";
    return sb.as_cslice().str();
}

schema::Trace TraceImpl::to_schema() const {
    schema::Trace result;
    result.trace_id = trace_id;
    result.external_hash = external_hash;
    result.start_lt = start_lt;
    result.start_utime = start_utime;
    result.end_lt = end_lt;
    result.end_utime = end_utime;
    result.type = type;

    for(auto & edge : edges) {
        result.edges.push_back({edge.trace_id, edge.msg_hash, edge.left_tx, edge.right_tx});
    }
    return result;
}



//
// TraceAssembler
//
void TraceAssembler::start_up() {
    alarm_timestamp() = td::Timestamp::in(1.0);
}

void TraceAssembler::alarm() {
    alarm_timestamp() = td::Timestamp::in(1.0);

    LOG(INFO) << " Pending traces: " << pending_traces_.size()
              << " Pending edges: " << pending_edges_.size()
              << " Broken edges count: " << broken_count_;
}


void TraceAssembler::assemble(std::int32_t seqno, ParsedBlockPtr block, td::Promise<ParsedBlockPtr> promise) {
    // promise.set_value(std::move(block));
    // return;

    queue_.emplace(seqno, Task{std::move(block), std::move(promise)});

    process_queue();
}

void TraceAssembler::update_expected_seqno(std::int32_t new_expected_seqno) { 
    expected_seqno_ = new_expected_seqno;
    LOG(INFO) << "Updating the expected senqo. New expected seqno is " << expected_seqno_;
    
    process_queue();
}

void TraceAssembler::process_queue() {
    auto it = queue_.find(expected_seqno_);
    while(it != queue_.end()) {
        process_block(it->second.block_);

        // block processed
        it->second.promise_.set_result(it->second.block_);
        queue_.erase(it);

        expected_seqno_ += 1;
        it = queue_.find(expected_seqno_);
    }
}

void TraceAssembler::process_transaction(schema::Transaction& tx) {
    // processing system nodes
    // nothing todo

    // // processing common nodes
    // auto it = pending_nodes_.find(tx.hash);
    // if(it == pending_nodes_.end()) {
    //     std::int32_t degree = 0;
    //     if (tx.in_msg.has_value()) {
    //         ++degree;
    //     }
    //     degree += tx.out_msgs.size();
    //     pending_nodes_.emplace(tx.hash, TraceNode{tx.hash, convert::to_raw_address(tx.account), degree});
    // }

    // process messages
    std::optional<TraceEdgeImpl> edge_;
    if (tx.in_msg.has_value()) {
        process_message(tx, tx.in_msg.value(), true);
    }
    for(const auto &msg : tx.out_msgs) {
        process_message(tx, msg, false);
    }
}

void TraceAssembler::process_message(schema::Transaction& tx, const schema::Message& msg, bool is_in_msg) {
    auto maybe_edge = std::optional<TraceEdgeImpl>();
    bool is_broken{false};
    // process special transactions
    {
        if (msg.source && msg.source.value() == "-1:0000000000000000000000000000000000000000000000000000000000000000" && tx.out_msgs.size() == 0) {
            maybe_edge = TraceEdgeImpl{td::Bits256{}, msg.hash, std::nullopt, tx.hash, 
                to_std_optional(msg.source), to_std_optional(msg.destination), false, 
                TraceEdgeImpl::Type::sys};
        }

        // external message
        if(!msg.source) {
            // LOG(INFO) << "Found an external message to " << msg.destination.value();
            maybe_edge = TraceEdgeImpl{td::Bits256{}, msg.hash, std::nullopt, tx.hash, 
                to_std_optional(msg.source), to_std_optional(msg.destination), false, 
                TraceEdgeImpl::Type::ext};
        }
        // log message
        if(!msg.destination) {
            // LOG(INFO) << "Found a log message from " << msg.source.value();
            maybe_edge = TraceEdgeImpl{td::Bits256{}, msg.hash, tx.hash, std::nullopt, 
                to_std_optional(msg.source), to_std_optional(msg.destination), false, 
                TraceEdgeImpl::Type::logs};
        }
    }
    if (!maybe_edge.has_value()) {
        auto it = pending_edges_.find(msg.hash);
        if (it != pending_edges_.end()) {
            // found an edge
            // TODO: process log messages
            if (is_in_msg) {
                it->second.right_tx = tx.hash;
            } else {
                LOG(ERROR) << "This is actually should not happen with complete edge! tx: " << tx.hash;
                it->second.left_tx = tx.hash;
                is_broken = true;
            }
            it->second.incomplete = false;
            maybe_edge = it->second;
            pending_edges_.erase(it);
        } else {
            // no edge found
            if (is_in_msg) {
                // LOG(ERROR) << "This is actually should not happen with incomplete edge! tx: " << tx.hash;
                broken_count_ += 1;
                auto edge = TraceEdgeImpl{td::Bits256{}, msg.hash, std::nullopt, tx.hash,
                    to_std_optional(msg.source), to_std_optional(msg.destination), true, 
                    TraceEdgeImpl::Type::ord};
                pending_edges_.emplace(msg.hash, edge);
                maybe_edge = edge;
            } else {
                auto edge = TraceEdgeImpl{td::Bits256{}, msg.hash, tx.hash, std::nullopt,
                    to_std_optional(msg.source), to_std_optional(msg.destination), true, 
                    TraceEdgeImpl::Type::ord};
                pending_edges_.emplace(msg.hash, edge);
                maybe_edge = edge;
            }
        }
    }

    // check if problem
    if(!maybe_edge.has_value()) {
        LOG(WARNING) << "I have no value, but why?";
        std::_Exit(42);
    }

    // updating pending traces
    auto edge = std::move(maybe_edge.value());
    TraceImplPtr left = nullptr;
    TraceImplPtr right = nullptr;
    if (edge.left_tx.has_value()){
        auto it = pending_traces_.find(edge.left_tx.value());
        if (it != pending_traces_.end()) {
            left = it->second;
        }
    }
    if (edge.right_tx.has_value()) {
        auto it = pending_traces_.find(edge.right_tx.value());
        if (it != pending_traces_.end()) {
            right = it->second;
        }
    }

    auto add_edge = [&](TraceImplPtr trace, const schema::Transaction& tx, const TraceEdgeImpl& edge) {
        if (edge.is_incomplete()) {
            // if (!edge.left_tx.has_value() && edge.right_tx.has_value()) {
            //     LOG(ERROR) << "Strange incomplete edge!";
            // }
            if (trace->txs.find(tx.hash) == trace->txs.end()) {
                trace->txs[tx.hash] = 1;
                this->pending_traces_[tx.hash] = trace;
            } else {
                ++trace->txs[tx.hash];
            }
            trace->edges.insert(edge);
        } else {
            // update left tx degree
            if (edge.left_tx.has_value()) {
                auto it = trace->txs.find(edge.left_tx.value());
                if (it == trace->txs.end()) {
                    LOG(WARNING) << "I haven't found the left tx in pending_traces_, but I should!"
                                 << edge.str();
                } else {
                    --it->second;
                }
            } else {
                if (edge.type == TraceEdgeImpl::Type::ord)
                    LOG(ERROR) << "It should be an external or system: " << edge.type;
            }
            // update right tx degree
            if (edge.right_tx.has_value()) {
                auto it = trace->txs.find(edge.right_tx.value());
                if(edge.right_tx != tx.hash) {
                    LOG(ERROR) << "Why right tx != tx.hash?";
                }
                if (it == trace->txs.end()) {
                    // LOG(WARNING) << "I haven't found the right tx in pending_traces_, but it is OK!"
                    //              << edge.str();
                    trace->txs[tx.hash] = 0;
                    this->pending_traces_[tx.hash] = trace;
                } else {
                    --it->second;
                }
            } else {
                if (edge.type == TraceEdgeImpl::Type::ord)
                    LOG(ERROR) << "It should be an log or system: " << edge.type;
            }
            
            if (trace->edges.find(edge) != trace->edges.end()) {
                // reinsert edge in case if incomplete edge exists
                trace->edges.erase(edge);
                trace->edges.insert(edge);
            } else {
                // insert non-ord edge and warn if it is ord (in this case the first branch should occur)
                if (edge.type == TraceEdgeImpl::Type::ord) {
                    LOG(WARNING) << "I've not found the incomplete variant of this transaction, and it is suspicious! Type: " << edge.type << " Edge: " << edge.str();
                }
                trace->edges.insert(edge);
            }
        }
    };
    // now there are 4 cases:
    if (!left && !right) {
        // if(edge.type == TraceEdgeImpl::Type::ord)
        //     LOG(ERROR) << "New trace with tx: " << tx.hash << " edge: " << edge.str();
        left = std::make_shared<TraceImpl>(tx);
        add_edge(left, tx, edge);
        updated_traces_.insert(left);
    } else if (!left && right) {
        LOG(INFO) << "Case 01, but i can't do this";
        LOG(WARNING) << edge.str();
    } else if (left && !right) {
        // LOG(INFO) << "Case 10: new edge.";
        // LOG(WARNING) << edge.str() << " tx.hash=" << td::base64_encode(tx.hash.as_slice());
        add_edge(left, tx, edge);
        updated_traces_.insert(left);
    } else if (left && right) {
        LOG(INFO) << "Case 11 and it is weird!";
        LOG(WARNING) << edge.str();
    }

    if (left) {
        tx.trace_id = left->trace_id;
    }
    if (is_broken) {
        left->type = TraceImpl::Type::broken;
    }
}

void TraceAssembler::process_block(ParsedBlockPtr block) {
    // sort by lt
    std::vector<std::reference_wrapper<schema::Transaction>> sorted_txs;
    for(auto& blk: block->blocks_) {
        for(auto& tx: blk.transactions) {
            sorted_txs.push_back(tx);
        }
    }
    std::sort(sorted_txs.begin(), sorted_txs.end(), [](auto& lhs, auto& rhs){
        if (lhs.get().lt != rhs.get().lt) {
            return lhs.get().lt < rhs.get().lt;
        }
        return (convert::to_raw_address(lhs.get().account) < convert::to_raw_address(rhs.get().account));
    });

    std::uint64_t last_lt = 0;
    for(auto &tx : sorted_txs) {
        if (last_lt > tx.get().lt) {
            LOG(ERROR) << "Wrong order!";
            for(auto& tx : sorted_txs) {
                LOG(WARNING) << tx.get().lt;
            }
        }
        last_lt = tx.get().lt;
        process_transaction(tx.get());
    }

    for(auto & trace : updated_traces_) {
        auto result = trace->to_schema();
        
        if (trace->type == TraceImpl::Type::new_trace) {
            trace->type = TraceImpl::Type::pending;
        }
        // prune edges
        {
            auto it = trace->edges.begin();
            while (it != trace->edges.end()) {
                if (!it->is_incomplete()) {
                    it = trace->edges.erase(it);
                } else {
                    ++it;
                }
            }
        }
        // prune txs
        {
            auto it = trace->txs.begin();
            while (it != trace->txs.end()) {
                if (it->second == 0) {
                    pending_traces_.erase(it->first);
                    it = trace->txs.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // if trace is complete
        if (trace->edges.size() == 0) {
            result.type = schema::Trace::Type::complete;
        }
        block->traces_.push_back(std::move(result));
    }
    // LOG(INFO) << "Assembled " << block->traces_.size() << " traces for a block";
    updated_traces_.clear();
}