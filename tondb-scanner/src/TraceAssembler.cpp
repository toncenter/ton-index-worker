#include <map>
#include <functional>

#include "TraceAssembler.h"
#include "td/utils/JsonBuilder.h"
#include "convert-utils.h"
#include "tddb/td/db/RocksDb.h"


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
    result.state = state;
    result.pending_edges_ = pending_edges;
    result.edges_ = edges;
    result.nodes_ = nodes;
    return result;
}

TraceAssembler::TraceAssembler(std::string db_path, size_t gc_distance) : 
        db_path_(db_path), gc_distance_(gc_distance) {
    auto kv = td::RocksDb::open(db_path);
    if (kv.is_error()) {
        LOG(FATAL) << "Failed to open RocksDB: " << kv.error();
    }
    kv_ = std::make_unique<td::RocksDb>(kv.move_as_ok());
}

void TraceAssembler::start_up() {
    alarm_timestamp() = td::Timestamp::in(60.0);
}

void TraceAssembler::alarm() {
    alarm_timestamp() = td::Timestamp::in(60.0);

    if (expected_seqno_ > gc_distance_) {
        auto gc_status = gc_states(expected_seqno_ - gc_distance_);
        if (gc_status.is_error()) {
            LOG(ERROR) << "Error while garbage collecting Trace Assembler states: " << gc_status.move_as_error();
        }
    }

    LOG(INFO) << " Pending traces: " << pending_traces_.size()
              << " Pending edges: " << pending_edges_.size()
              << " Broken traces: " << broken_count_;
}

void TraceAssembler::assemble(ton::BlockSeqno seqno, ParsedBlockPtr block, td::Promise<ParsedBlockPtr> promise) {
    queue_.emplace(seqno, Task{seqno, std::move(block), std::move(promise)});

    process_queue();
}

void TraceAssembler::set_expected_seqno(ton::BlockSeqno new_expected_seqno) { 
    expected_seqno_ = new_expected_seqno;
}

void printStringAsHex(const std::string& str) {
    for (unsigned char c : str) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << " ";
    }
    std::cout << std::dec << std::endl; // Reset to decimal output
}

std::string seqno_to_rocksdb_key(ton::BlockSeqno seqno) {
    uint32_t big_endian_key = htobe32(seqno);
    const char* key_data = reinterpret_cast<const char*>(&big_endian_key);
    return std::string(key_data, sizeof(ton::BlockSeqno));
}

ton::BlockSeqno rocksdb_key_to_seqno(td::Slice key) {
    const ton::BlockSeqno *value_raw = reinterpret_cast<const ton::BlockSeqno *>(key.data());
    return be32toh(*value_raw);
}

td::Result<ton::BlockSeqno> TraceAssembler::restore_state(ton::BlockSeqno seqno) {
    auto snapshot = kv_->snapshot();
    bool found = false;
    auto state_seqno = seqno;
    auto from_seqno = seqno > gc_distance_ ? seqno - gc_distance_ : 0;
    while (!found && (state_seqno > from_seqno)) {
        std::string buffer;
        auto key = std::to_string(state_seqno);
        auto S = snapshot->get(key, buffer);
        if (S.is_error()) {
            LOG(ERROR) << "Failed to get state for seqno " << state_seqno << ": " << S.move_as_error();
            state_seqno -= 1;
            continue;
        }
        auto status = S.move_as_ok();
        if (status == td::KeyValue::GetStatus::NotFound) {
            state_seqno -= 1;
            continue;
        }

        std::unordered_map<td::Bits256, TraceImplPtr, Bits256Hasher> pending_traces;
        std::unordered_map<td::Bits256, TraceEdgeImpl, Bits256Hasher> pending_edges;    
        try {
            size_t offset = 0;
            msgpack::unpacked pending_traces_res;
            msgpack::unpack(pending_traces_res, buffer.data(), buffer.size(), offset);
            msgpack::object pending_traces_obj = pending_traces_res.get();

            msgpack::unpacked pending_edges_res;
            msgpack::unpack(pending_edges_res, buffer.data(), buffer.size(), offset);
            msgpack::object pending_edges_obj = pending_edges_res.get();

            pending_traces_obj.convert(pending_traces);
            pending_edges_obj.convert(pending_edges);
        } catch (const std::exception &e) {
            LOG(ERROR) << "Failed to unpack state for seqno " << state_seqno << ": " << e.what();
            state_seqno -= 1;
            continue;
        }
        
        pending_traces_ = std::move(pending_traces);
        pending_edges_ = std::move(pending_edges);
        found = true;
    }

    if (!found) {
        return td::Status::Error(ton::ErrorCode::warning, "TraceAssembler state not found");
    }
    
    return state_seqno;
}

td::Status TraceAssembler::save_state(ton::BlockSeqno seqno) {
    std::stringstream buffer;
    msgpack::pack(buffer, pending_traces_);
    msgpack::pack(buffer, pending_edges_);
 
    return kv_->set(std::to_string(seqno), buffer.str());
}

td::Status TraceAssembler::gc_states(ton::BlockSeqno before_seqno) {
    std::string last_gcd_seqno_value;
    auto res = kv_->get("last_gcd", last_gcd_seqno_value);
    if (res.is_error() || res.ok() == td::KeyValueReader::GetStatus::NotFound) {
        TRY_STATUS(kv_->set("last_gcd", std::to_string(before_seqno)));
        return td::Status::OK();
    }
    ton::BlockSeqno last_gcd_seqno = std::stoul(last_gcd_seqno_value);
    
    TRY_STATUS(kv_->begin_write_batch());
    for (auto c = last_gcd_seqno; c < before_seqno; c++) {
        TRY_STATUS(kv_->erase(std::to_string(c)));
    }
    TRY_STATUS(kv_->set("last_gcd", std::to_string(before_seqno)));
    TRY_STATUS(kv_->begin_write_batch());
    return td::Status::OK();
}

void TraceAssembler::process_queue() {
    auto it = queue_.find(expected_seqno_);
    while(it != queue_.end()) {
        process_block(it->second.seqno_, it->second.block_);
        it->second.promise_.set_result(it->second.block_);

        // block processed
        queue_.erase(it);

        auto save_status = save_state(expected_seqno_);
        if (save_status.is_error()) {
            LOG(ERROR) << "Error while saving Trace Assembler state: " << save_status.move_as_error();
        }

        expected_seqno_ += 1;
        it = queue_.find(expected_seqno_);
    }
}

void TraceAssembler::process_block(ton::BlockSeqno seqno, ParsedBlockPtr block) {
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
        if (lhs.get().account.workchain != rhs.get().account.workchain) {
            return (lhs.get().account.workchain < rhs.get().account.workchain);
        }
        return (lhs.get().account.addr < rhs.get().account.addr);
    });

    // process transactions
    std::vector<TraceEdgeImpl> completed_edges;
    std::unordered_set<td::Bits256, Bits256Hasher> updated_traces;
    std::unordered_set<td::Bits256, Bits256Hasher> pending_edges_added;
    for(auto &tx : sorted_txs) {
        process_transaction(seqno, tx.get(), completed_edges, updated_traces, pending_edges_added);
    }
    std::unordered_map<td::Bits256, schema::Trace, Bits256Hasher> trace_map;
    for (auto &trace_id : updated_traces) {
        auto trace = pending_traces_[trace_id];
        if (trace->pending_edges == 0) {
            if (trace->state != TraceImpl::State::broken) {
                trace->state = TraceImpl::State::complete;
            }
            pending_traces_.erase(trace->trace_id);
        } else if (trace->pending_edges < 0) {
            trace->state = TraceImpl::State::broken;
        }
        trace_map[trace_id] = trace->to_schema();
    }
    std::vector<TraceEdgeImpl> block_all_edges = completed_edges;
    for (const auto &edge_hash : pending_edges_added) {
        const auto &edge = pending_edges_[edge_hash];
        assert(edge.incomplete);
        block_all_edges.push_back(edge);
    }
    for (const auto &edge : block_all_edges) {
        // update trace
        auto &trace_schema = trace_map[edge.trace_id];
        trace_schema.edges.push_back(edge.to_schema());
    }
    for (auto &[_, trace] : trace_map) {
        block->traces_.push_back(std::move(trace));
    }
}

void TraceAssembler::process_transaction(ton::BlockSeqno seqno, schema::Transaction& tx, std::vector<TraceEdgeImpl>& completed_edges, 
        std::unordered_set<td::Bits256, Bits256Hasher>& updated_traces, std::unordered_set<td::Bits256, Bits256Hasher>& pending_edges_added) {
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
                } else if (msg.source.value() == "-1:0000000000000000000000000000000000000000000000000000000000000000") {
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
                trace->edges += !edge.incomplete;
                trace->pending_edges += edge.incomplete;
                if(edge.broken) {
                    trace->state = TraceImpl::State::broken;
                    ++broken_count_;
                }
                if (edge.incomplete) {
                    pending_edges_.insert_or_assign(edge.msg_hash, edge);
                    pending_edges_added.insert(edge.msg_hash);
                } else {
                    completed_edges.push_back(edge);
                }
                pending_traces_.insert_or_assign(trace->trace_id, trace);
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
                        // LOG(ERROR) << "Broken trace for in_msg of tx: " << tx.hash;
                        // create a broken trace
                        trace = std::make_shared<TraceImpl>(seqno, tx);
                        trace->edges += !edge.incomplete;
                        trace->pending_edges += edge.incomplete;
                        trace->state = TraceImpl::State::broken;
                        
                        ++broken_count_;
                        edge.trace_id = trace->trace_id;
                        pending_traces_.insert_or_assign(trace->trace_id, trace);
                    } else {
                        trace = trace_it->second;
                    }
                }

                completed_edges.push_back(edge);
                pending_edges_.erase(edge_it);
                pending_edges_added.erase(edge.msg_hash);

                trace->pending_edges -= 1;
                trace->edges += 1;
                trace->nodes += 1;
            }
        }
        updated_traces.insert(trace->trace_id);

        tx.trace_id = trace->trace_id;
        msg.trace_id = trace->trace_id;
    } else {
        trace = std::make_shared<TraceImpl>(seqno, tx);
        pending_traces_.insert_or_assign(trace->trace_id, trace);
        updated_traces.insert(trace->trace_id);

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
        
        trace->pending_edges += edge.incomplete;
        trace->edges += !edge.incomplete;
        if (edge.incomplete) {
            pending_edges_.insert_or_assign(edge.msg_hash, edge);
            pending_edges_added.insert(edge.msg_hash);
        } else {
            completed_edges.push_back(edge);
        }
        msg.trace_id = trace->trace_id;
    }
}
