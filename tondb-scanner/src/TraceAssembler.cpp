#include <map>
#include <functional>

#include "TraceAssembler.h"
#include "convert-utils.h"

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
       << trace_id << ", "
       << msg_hash << ", " 
       << (left_tx.has_value() ? B64HASH(left_tx.value()) : "null") << ", "
       << (right_tx.has_value() ? B64HASH(right_tx.value()) : "null") << ")";
    return sb.as_cslice().str();
}

std::string TraceImpl::str() const {
    td::StringBuilder sb;
    sb << "Trace(" << trace_id 
       << ", nodes="  << nodes_ 
       << ", edges=" << edges_ 
       << ", pending_edges=" << pending_edges_ << ")";
    return sb.as_cslice().str();
}

schema::TraceEdge TraceEdgeImpl::to_schema() const {
    schema::TraceEdge result;
    result.trace_id = trace_id;
    result.msg_hash = msg_hash;
    result.msg_lt = msg_lt;
    result.left_tx = left_tx;
    result.right_tx = right_tx;
    result.type = type;
    result.incomplete = incomplete;
    result.broken = broken;
    return result;
}

schema::Trace TraceImpl::to_schema() const {
    schema::Trace result;
    result.trace_id = trace_id;
    result.external_hash = external_hash;
    result.mc_seqno_start = mc_seqno_start;
    result.mc_seqno_end = mc_seqno_end;
    result.start_lt = start_lt;
    result.start_utime = start_utime;
    result.end_lt = end_lt;
    result.end_utime = end_utime;
    result.state = type;
    result.pending_edges_ = pending_edges_;
    result.edges_ = edges_;
    result.nodes_ = nodes_;
    return result;
}

//
// TraceAssembler
//
void TraceAssembler::start_up() {
    alarm_timestamp() = td::Timestamp::in(10.0);
}

void TraceAssembler::alarm() {
    alarm_timestamp() = td::Timestamp::in(10.0);

    LOG(INFO) << " Pending traces: " << pending_traces_.size()
              << " Pending edges: " << pending_edges_.size()
              << " Broken traces: " << broken_count_;
}

void TraceAssembler::assemble(std::int32_t seqno, ParsedBlockPtr block, td::Promise<ParsedBlockPtr> promise) {
    // promise.set_value(std::move(block));
    // return;
    queue_.emplace(seqno, Task{seqno, std::move(block), std::move(promise)});

    if (is_ready_) {
        process_queue();
    }
}

void TraceAssembler::update_expected_seqno(std::int32_t new_expected_seqno) { 
    expected_seqno_ = new_expected_seqno;
    LOG(INFO) << "Updating the expected senqo. New expected seqno is " << expected_seqno_;
    if (is_ready_) {
        process_queue();
    }
}

void TraceAssembler::process_queue() {
    auto it = queue_.find(expected_seqno_);
    while(it != queue_.end()) {
        process_block(it->second.seqno_, it->second.block_);
        it->second.promise_.set_result(it->second.block_);

        // block processed
        queue_.erase(it);
        expected_seqno_ += 1;
        it = queue_.find(expected_seqno_);
    }
}

void TraceAssembler::process_block(std::int32_t seqno, ParsedBlockPtr block) {
    // sort transactions by lt
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

    // process transactions
    for(auto &tx : sorted_txs) {
        process_transaction(seqno, tx.get());
    }
    std::unordered_map<td::Bits256, std::size_t, Bits256Hasher> trace_map;
    std::vector<schema::Trace> traces;
    for (auto & trace_id : updated_traces_) {
        auto trace = pending_traces_[trace_id];
        if(trace->pending_edges_ == 0) {
            if (trace->type != TraceImpl::State::broken) {
                trace->type = TraceImpl::State::complete;
            }
            pending_traces_.erase(trace->trace_id);
        } else if (trace->pending_edges_ < 0) {
            trace->type = TraceImpl::State::broken;
        }
        traces.push_back(trace->to_schema());
        trace_map[trace_id] = traces.size() - 1;
    }
    for (auto & edge_hash : updated_edges_) {
        auto edge = pending_edges_.find(edge_hash);
        if (edge == pending_edges_.end()) {
            LOG(ERROR) << "No edge found!";
            std::_Exit(42);
        }

        // update trace
        auto &trace_schema = traces[trace_map[edge->second.trace_id]];
        trace_schema.edges.push_back(edge->second.to_schema());

        if (!edge->second.incomplete) {
            pending_edges_.erase(edge);
        }
    }
    block->traces_ = std::move(traces);

    // tear down
    updated_traces_.clear();
    updated_edges_.clear();
}

void TraceAssembler::process_transaction(std::int32_t seqno, schema::Transaction& tx) {
    TraceImplPtr trace = nullptr;
    if (tx.in_msg.has_value()) {
        auto &msg = tx.in_msg.value();
        TraceEdgeImpl edge;
        {
            auto edge_it = pending_edges_.find(msg.hash);
            if (edge_it == pending_edges_.end()) {
                // edge doesn't exist
                if (!msg.source) {
                    // external
                    edge.trace_id = tx.hash;
                    edge.msg_hash = msg.hash;
                    edge.msg_lt = (msg.created_lt ? msg.created_lt.value() : 0);
                    edge.left_tx = std::nullopt;
                    edge.right_tx = tx.hash;
                    edge.type = TraceEdgeImpl::Type::ext;
                    edge.incomplete = false;
                    edge.broken = false;
                } else if (msg.source && msg.source.value() == "-1:0000000000000000000000000000000000000000000000000000000000000000") {
                    // system
                    edge.trace_id = tx.hash;
                    edge.msg_hash = msg.hash;
                    edge.msg_lt = (msg.created_lt ? msg.created_lt.value() : 0);
                    edge.left_tx = std::nullopt;
                    edge.right_tx = tx.hash;
                    edge.type = TraceEdgeImpl::Type::sys;
                    edge.incomplete = false;
                    edge.broken = false;
                } else {
                    // broken edge
                    edge.trace_id = tx.hash;
                    edge.msg_hash = msg.hash;
                    edge.msg_lt = (msg.created_lt ? msg.created_lt.value() : 0);
                    edge.left_tx = std::nullopt;
                    edge.right_tx = tx.hash;
                    edge.type = TraceEdgeImpl::Type::ord;
                    edge.incomplete = true;
                    edge.broken = true;
                }

                // trace
                trace = std::make_shared<TraceImpl>(seqno, tx);
                trace->edges_ += !edge.incomplete;
                trace->pending_edges_ += edge.incomplete;
                if(edge.broken) {
                    trace->type = TraceImpl::State::broken;
                }

                pending_edges_.insert({edge.msg_hash, edge});
                pending_traces_.insert({trace->trace_id, trace});
            } else {
                // edge exists
                edge_it->second.right_tx = tx.hash;
                edge_it->second.incomplete = false;
                edge_it->second.broken = false;
                edge = edge_it->second;

                // trace
                {
                    auto trace_it = pending_traces_.find(edge.trace_id);
                    if (trace_it == pending_traces_.end()) {
                        LOG(ERROR) << "Broken trace for in_msg of tx: " << tx.hash;
                        // create a broken trace
                        trace = std::make_shared<TraceImpl>(seqno, tx);
                        trace->edges_ += !edge.incomplete;
                        trace->pending_edges_ += edge.incomplete;
                        trace->type = TraceImpl::State::broken;
                        
                        ++broken_count_;
                        edge.trace_id = trace->trace_id;
                        pending_traces_.insert({trace->trace_id, trace});
                    } else {
                        trace = trace_it->second;
                    }
                }
                trace->pending_edges_ -= 1;
                trace->edges_ += 1;
                trace->nodes_ += 1;
            }
        }

        updated_edges_.insert(edge.msg_hash);
        updated_traces_.insert(trace->trace_id);

        tx.trace_id = trace->trace_id;
        msg.trace_id = trace->trace_id;
    } else {
        trace = std::make_shared<TraceImpl>(seqno, tx);
        pending_traces_.insert({trace->trace_id, trace});

        updated_traces_.insert(trace->trace_id);

        tx.trace_id = trace->trace_id;
    }
    // update trace meta
    trace->mc_seqno_end = std::max(trace->mc_seqno_end, seqno);
    trace->end_lt = std::max(trace->end_lt, tx.lt);
    trace->end_utime = std::max(trace->end_utime, tx.now);

    // out_msgs
    for(auto & msg : tx.out_msgs) {
        auto edge = TraceEdgeImpl{trace->trace_id, msg.hash, (msg.created_lt ? msg.created_lt.value() : 0), tx.hash, std::nullopt, TraceEdgeImpl::Type::ord, true, false};
        if (!msg.destination) {
            edge.type = TraceEdgeImpl::Type::logs;
            edge.incomplete = false;
            edge.broken = false;
        }
        pending_edges_.insert({edge.msg_hash, edge});

        if(msg.created_lt) {
            trace->end_lt = std::max(trace->end_lt, msg.created_lt.value());
        }
        if(msg.created_at) {
            trace->end_utime = std::max(trace->end_utime, msg.created_at.value());
        }

        trace->pending_edges_ += edge.incomplete;
        trace->edges_ += !edge.incomplete;

        updated_edges_.insert(edge.msg_hash);

        msg.trace_id = trace->trace_id;
    }
}
