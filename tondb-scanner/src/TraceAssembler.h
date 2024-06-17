#pragma once
#include "IndexData.h"
#include <queue>


//
// Traces
//
struct Bits256Hasher {
  std::size_t operator()(const td::Bits256& k) const {
    std::size_t seed = 0;
    for(const auto& el : k.as_array()) {
        seed ^= std::hash<td::uint8>{}(el) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

struct TraceEdgeImpl {
  td::Bits256 trace_id;
  td::Bits256 msg_hash;
  std::optional<td::Bits256> left_tx;
  std::optional<td::Bits256> right_tx;
  
  std::optional<std::string> src{std::nullopt};
  std::optional<std::string> dest{std::nullopt};

  bool incomplete{false};
  enum Type { ord = 0, sys = 1, ext = 2, logs = 3 } type{Type::ord};
  
  bool is_incomplete() const { return incomplete; }
  bool is_system() const { return type == Type::sys; }
  bool is_external() const { return type == Type::ext; }
  bool is_log() const { return type == Type::logs; }
  std::string str() const;

  struct Hasher {
    bool operator()(const TraceEdgeImpl& edge) const {
      return Bits256Hasher{}(edge.msg_hash);
    }
  };

  friend bool operator==(const TraceEdgeImpl& lhs, const TraceEdgeImpl& rhs) {
    return lhs.msg_hash == rhs.msg_hash;
  }
};

struct TraceImpl {
  using Type = schema::Trace::Type;

  td::Bits256 trace_id;
  std::optional<td::Bits256> external_hash;
  std::int32_t mc_seqno;

  // meta
  std::uint64_t start_lt;
  std::uint32_t start_utime;

  std::uint64_t end_lt{0};
  std::uint32_t end_utime{0};

  Type type{Type::new_trace};

  std::unordered_map<td::Bits256, std::int32_t, Bits256Hasher> txs;
  std::unordered_set<TraceEdgeImpl, TraceEdgeImpl::Hasher> edges;

  TraceImpl(const schema::Transaction &tx) :
    trace_id(tx.hash), external_hash((tx.in_msg.has_value() ? std::optional<td::Bits256>(tx.in_msg.value().hash) : std::nullopt)),
    start_lt(tx.lt), start_utime(tx.now), type(Type::new_trace) {}

  bool is_finished() { return edges.size() == 0; }

  // utils
  std::string str() const;
  schema::Trace to_schema() const;

  struct Hasher {
    bool operator()(const TraceImpl& trace) const {
      return Bits256Hasher{}(trace.trace_id);
    }
  };

  friend bool operator==(const TraceImpl& lhs, const TraceImpl& rhs) {
    return lhs.trace_id == rhs.trace_id;
  }
};
using TraceImplPtr = std::shared_ptr<TraceImpl>;


// 
// TraceAssembler
//
class TraceAssembler: public td::actor::Actor {
    struct Task {
        ParsedBlockPtr block_;
        td::Promise<ParsedBlockPtr> promise_;
    };

    // block queue
    std::int32_t expected_seqno_;
    std::map<std::int32_t, Task> queue_;

    // trace assembler
    std::uint64_t broken_count_{0};
    std::unordered_map<td::Bits256, TraceEdgeImpl, Bits256Hasher> pending_edges_;
    std::unordered_map<td::Bits256, TraceImplPtr, Bits256Hasher> pending_traces_;

    std::unordered_set<TraceImplPtr> updated_traces_;
public:
    TraceAssembler(std::int32_t expected_seqno) : expected_seqno_(expected_seqno), pending_edges_({}) {}
    
    void assemble(int mc_seqno, ParsedBlockPtr mc_block_, td::Promise<ParsedBlockPtr> promise);
    void update_expected_seqno(std::int32_t new_expected_seqno);
    void process_queue();

    void start_up() override;
    void alarm() override;
private:
    void process_block(ParsedBlockPtr block);
    void process_transaction(schema::Transaction& tx);
    void process_message(schema::Transaction& tx, const schema::Message& msg, bool is_in_msg);
};
