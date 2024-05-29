#pragma once
#include <any>
#include "td/actor/actor.h"
#include "DbScanner.h"

using TraceId = td::Bits256;

struct Trace {
    TraceId id;
    ton::WorkchainId workchain;
    std::vector<Trace *> children;
    td::Ref<vm::Cell> transaction_root;

    ~Trace() {
        for (Trace* child : children) {
            delete child;
        }
    }

    int depth() const {
        int res = 0;
        for (const auto child : children) {
            res = std::max(res, child->depth());
        }
        return res + 1;
    }

    int transactions_count() const {
        int res = transaction_root.not_null() ? 1 : 0;
        for (const auto& child : children) {
            res += child->transactions_count();
        }
        return res;
    }

    std::string to_string(int tabs = 0) const {
        std::stringstream ss;
        ss << std::endl;
        for (int i = 0; i < tabs; i++) {
            ss << "--";
        }
        // print account, lt, status, bounced, in_msg opcode
        block::gen::Transaction::Record trans;
        block::gen::TransactionDescr::Record_trans_ord descr;
        tlb::unpack_cell(transaction_root, trans);
        tlb::unpack_cell(trans.description, descr);
        ss << "TX acc=" << trans.account_addr.to_hex() << " lt=" << trans.lt << " outmsg_cnt=" << trans.outmsg_cnt << " aborted=" << descr.aborted << std::endl;

        for (const auto child : children) {
            ss << child->to_string(tabs + 1);
        }
        return ss.str();
    }
};

struct OutMsgInfo {
    td::Bits256 hash;
    block::StdAddress destination;
    td::Ref<vm::Cell> root;
};


struct TransactionInfo {
    block::StdAddress account;
    td::Bits256 hash;
    ton::LogicalTime lt;
    td::Ref<vm::Cell> root;
    td::Bits256 in_msg_hash;
    bool is_first;
    std::vector<OutMsgInfo> out_msgs;
    std::optional<td::Bits256> initial_msg_hash{}; // hash of initial transaction in block that caused this transaction. 
                                                 // This is not necessarily ext in message, because ext in could happen in prev block.
};

class TraceEmulatorScheduler : public td::actor::Actor {
  private: 
    td::actor::ActorId<DbScanner> db_scanner_;
    std::uint32_t stats_timeout_;

    ton::BlockSeqno last_known_seqno_{0};

    std::queue<std::uint32_t> queued_seqnos_;
    td::Timestamp next_print_stats_;
    std::unordered_set<std::uint32_t> seqnos_fetching_;
    std::unordered_set<std::uint32_t> seqnos_emulating_;
    std::set<std::uint32_t> seqnos_processed_;
    std::queue<std::pair<std::uint32_t, MasterchainBlockDataState>> blocks_to_emulate_;
    std::uint32_t blocks_to_emulate_queue_max_size_{1000};

    // td::Timestamp last_tps_calc_ts_ = td::Timestamp::now();
    // uint32_t last_tps_calc_processed_count_{0};
    // float tps_{0};

  public:
    TraceEmulatorScheduler(td::actor::ActorId<DbScanner> db_scanner, std::uint32_t stats_timeout = 60) :
        db_scanner_(db_scanner), stats_timeout_(stats_timeout) {};
    virtual ~TraceEmulatorScheduler() = default;

    virtual void start_up() override;

  
    void got_last_mc_seqno(ton::BlockSeqno last_known_seqno);

    void fetch_error(std::uint32_t seqno, td::Status error);

    void seqno_fetched(std::uint32_t seqno, MasterchainBlockDataState mc_data_state);

    void emulate_mc_shard(MasterchainBlockDataState mc_data_state);

    void parse_error(std::uint32_t seqno, td::Status error);

    void block_parsed(ton::BlockId blk_id, std::vector<TransactionInfo> txs);

    void alarm();
};
