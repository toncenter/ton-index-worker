#include <fstream>
#include <sstream>
#include <string>
#include <common/delay.h>
#include <tdutils/td/utils/filesystem.h>
#include <emulator/transaction-emulator.h>
#include "TraceEmulator.h"
#include "TraceInserter.h"


struct BitArrayHasher {
    std::size_t operator()(const td::Bits256& k) const {
        std::size_t seed = 0;
        for(const auto& el : k.as_array()) {
            seed ^= std::hash<td::uint8>{}(el) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

struct AddressHasher {
    std::size_t operator()(const block::StdAddress& addr) const {
        return std::hash<td::uint32>{}(addr.workchain) ^ BitArrayHasher()(addr.addr);
    }
};

class BlockParser: public td::actor::Actor {
    td::Ref<ton::validator::BlockData> block_data_;
    td::Promise<std::vector<TransactionInfo>> promise_;
public:
    BlockParser(td::Ref<ton::validator::BlockData> block_data, td::Promise<std::vector<TransactionInfo>> promise)
        : block_data_(std::move(block_data)), promise_(std::move(promise)) {}

    void start_up() override {
        std::vector<TransactionInfo> res;

        block::gen::Block::Record blk;
        block::gen::BlockInfo::Record info;
        block::gen::BlockExtra::Record extra;
        if (!(tlb::unpack_cell(block_data_->root_cell(), blk) && tlb::unpack_cell(blk.info, info) && tlb::unpack_cell(blk.extra, extra))) {
            promise_.set_error(td::Status::Error("block data info extra unpack failed"));
            stop();
            return;
        }
        try {
            vm::AugmentedDictionary acc_dict{vm::load_cell_slice_ref(extra.account_blocks), 256, block::tlb::aug_ShardAccountBlocks};

            td::Bits256 cur_addr = td::Bits256::zero();
            bool eof = false;
            bool allow_same = true;
            while (!eof) {
                auto value = acc_dict.extract_value(
                    acc_dict.vm::DictionaryFixed::lookup_nearest_key(cur_addr.bits(), 256, true, allow_same));
                if (value.is_null()) {
                    eof = true;
                    break;
                }
                allow_same = false;
                block::gen::AccountBlock::Record acc_blk;
                if (!(tlb::csr_unpack(std::move(value), acc_blk) && acc_blk.account_addr == cur_addr)) {
                    promise_.set_error(td::Status::Error("invalid AccountBlock for account " + cur_addr.to_hex()));
                    stop();
                    return;
                }
                vm::AugmentedDictionary trans_dict{vm::DictNonEmpty(), std::move(acc_blk.transactions), 64,
                                                    block::tlb::aug_AccountTransactions};
                td::BitArray<64> cur_trans{(long long)0};
                while (true) {
                    auto tvalue = trans_dict.extract_value_ref(
                        trans_dict.vm::DictionaryFixed::lookup_nearest_key(cur_trans.bits(), 64, true));
                    if (tvalue.is_null()) {
                        break;
                    }
                    block::gen::Transaction::Record trans;
                    if (!tlb::unpack_cell(tvalue, trans)) {
                        promise_.set_error(td::Status::Error("Failed to unpack Transaction"));
                        stop();
                        return;
                    }
                    block::gen::TransactionDescr::Record_trans_ord descr;
                    if (!tlb::unpack_cell(trans.description, descr)) {
                        LOG(WARNING) << "Skipping non ord transaction " << tvalue->get_hash().to_hex();
                        continue;
                    }

                    TransactionInfo tx_info;

                    tx_info.account = block::StdAddress(block_data_->block_id().id.workchain, cur_addr);
                    tx_info.hash = tvalue->get_hash().bits();
                    tx_info.root = tvalue;
                    tx_info.lt = trans.lt;

                    if (trans.r1.in_msg->prefetch_long(1)) {
                        auto msg = trans.r1.in_msg->prefetch_ref();
                        tx_info.in_msg_hash = msg->get_hash().bits();
                        auto message_cs = vm::load_cell_slice(trans.r1.in_msg->prefetch_ref());
                        tx_info.is_first = block::gen::t_CommonMsgInfo.get_tag(message_cs) == block::gen::CommonMsgInfo::ext_in_msg_info;
                    } else {
                        LOG(ERROR) << "Ordinary transaction without in_msg, skipping";
                        continue;
                    }

                    // LOG(INFO) << "TX hash: " << tx_info.hash.to_hex();

                    if (trans.outmsg_cnt != 0) {
                        vm::Dictionary dict{trans.r1.out_msgs, 15};
                        for (int x = 0; x < trans.outmsg_cnt; x++) {
                            auto value = dict.lookup_ref(td::BitArray<15>{x});
                            OutMsgInfo out_msg_info;
                            out_msg_info.hash = value->get_hash().bits();
                            out_msg_info.root = value;
                            tx_info.out_msgs.push_back(std::move(out_msg_info));

                            // LOG(INFO) << "  out msg: " << out_msg_info.hash.to_hex();
                        }
                    }

                    res.push_back(tx_info);
                }
            }
        } catch (vm::VmError err) {
            promise_.set_error(td::Status::Error(PSLICE() << "error while parsing AccountBlocks : " << err.get_msg()));
            stop();
            return;
        }
        promise_.set_value(std::move(res));
        stop();
        return;
    }
};

class TraceEmulator: public td::actor::Actor {
    std::shared_ptr<emulator::TransactionEmulator> emulator_;
    std::vector<td::Ref<vm::Cell>> shard_states_;
    std::unordered_map<block::StdAddress, std::shared_ptr<block::Account>, AddressHasher> emulated_accounts_;
    block::StdAddress account_addr_;
    td::Ref<vm::Cell> in_msg_;
    td::Promise<Trace *> promise_;
    size_t depth_{20};
    size_t pending_{0};
    Trace *result_;
public:
    TraceEmulator(std::shared_ptr<emulator::TransactionEmulator> emulator, std::vector<td::Ref<vm::Cell>> shard_states, 
                  std::unordered_map<block::StdAddress, std::shared_ptr<block::Account>, AddressHasher> emulated_accounts, td::Ref<vm::Cell> in_msg, size_t depth, td::Promise<Trace *> promise)
        : emulator_(std::move(emulator)), shard_states_(shard_states), emulated_accounts_(emulated_accounts), 
          in_msg_(std::move(in_msg)), depth_(depth), promise_(std::move(promise)) {
        
    }

    void set_error(td::Status error) {
        promise_.set_error(std::move(error));
        stop();
    }

    void start_up() override {
        auto message_cs = vm::load_cell_slice(in_msg_);
        int msg_tag = block::gen::t_CommonMsgInfo.get_tag(message_cs);
        if (msg_tag == block::gen::CommonMsgInfo::ext_in_msg_info) {
            block::gen::CommonMsgInfo::Record_ext_in_msg_info info;
            if (!(tlb::unpack(message_cs, info) && block::tlb::t_MsgAddressInt.extract_std_address(info.dest, account_addr_))) {
                set_error(td::Status::Error(PSLICE() << "Can't unpack external message " << in_msg_->get_hash()));
                return;
            }
        } else if (msg_tag == block::gen::CommonMsgInfo::int_msg_info) {
            block::gen::CommonMsgInfo::Record_int_msg_info info;
            if (!(tlb::unpack(message_cs, info) && block::tlb::t_MsgAddressInt.extract_std_address(info.dest, account_addr_))) {
                set_error(td::Status::Error(PSLICE() << "Can't unpack internal message " << in_msg_->get_hash()));
                return;
            }
        } else {
            LOG(ERROR) << "Ext out message found " << in_msg_->get_hash();
            promise_.set_error(td::Status::Error("Ext out message found"));
            return;
        }

        auto account_it = emulated_accounts_.find(account_addr_);
        if (account_it != emulated_accounts_.end()) {
            auto account = std::make_unique<block::Account>(*account_it->second);
            emulate_transaction(std::move(account));
            return;
        }

        for (const auto &shard_state : shard_states_) {
            block::gen::ShardStateUnsplit::Record sstate;
            if (!tlb::unpack_cell(shard_state, sstate)) {
                set_error(td::Status::Error("Failed to unpack ShardStateUnsplit"));
                stop();
                return;
            }
            block::ShardId shard;
            if (!(shard.deserialize(sstate.shard_id.write()))) {
                set_error(td::Status::Error("Failed to deserialize ShardId"));
                return;
            }
            if (shard.workchain_id == account_addr_.workchain && ton::shard_contains(ton::ShardIdFull(shard).shard, account_addr_.addr)) {
                vm::AugmentedDictionary accounts_dict{vm::load_cell_slice_ref(sstate.accounts), 256, block::tlb::aug_ShardAccounts};
                auto account = unpack_account(accounts_dict);
                if (!account) {
                    set_error(td::Status::Error("Failed to unpack account"));
                    return;
                }

                emulate_transaction(std::move(account));
                return;
            }
        }
        set_error(td::Status::Error("Account not found in shard_states"));
    }

    std::unique_ptr<block::Account> unpack_account(vm::AugmentedDictionary& accounts_dict) {
        auto ptr = std::make_unique<block::Account>(account_addr_.workchain, account_addr_.addr.cbits());
        auto account = accounts_dict.lookup(account_addr_.addr);
        if (account.is_null()) {
            if (!ptr->init_new(emulator_->get_unixtime())) {
                return nullptr;
            }
        } else if (!ptr->unpack(std::move(account), emulator_->get_unixtime(), 
                                account_addr_.workchain == ton::masterchainId && emulator_->get_config().is_special_smartcontract(account_addr_.addr))) {
            return nullptr;
        }
        ptr->block_lt = ptr->last_trans_lt_ - ptr->last_trans_lt_ % block::ConfigInfo::get_lt_align(); // TODO: check if it's correct
        return ptr;
    }

    void emulate_transaction(std::unique_ptr<block::Account> account) {
        auto emulation_r = emulator_->emulate_transaction(std::move(*account), in_msg_, 0, 0, block::transaction::Transaction::tr_ord);
        if (emulation_r.is_error()) {
            set_error(emulation_r.move_as_error());
            return;
        }
        auto emulation = emulation_r.move_as_ok();

        auto external_not_accepted = dynamic_cast<emulator::TransactionEmulator::EmulationExternalNotAccepted *>(emulation.get());
        if (external_not_accepted) {
            set_error(td::Status::Error(PSLICE() << "EmulationExternalNotAccepted: " << external_not_accepted->vm_exit_code));
            return;
        }

        auto emulation_success = dynamic_cast<emulator::TransactionEmulator::EmulationSuccess&>(*emulation);
        
        result_ = new Trace();
        result_->emulated = true;
        result_->workchain = account->workchain;
        result_->transaction_root = emulation_success.transaction;

        // LOG(INFO) << emulation_success.vm_log;
        
        block::gen::Transaction::Record trans;
        if (!tlb::unpack_cell(emulation_success.transaction, trans)) {
            set_error(td::Status::Error("Failed to unpack emulated Transaction"));
            return;
        }
        
        if (depth_ > 0 && trans.outmsg_cnt > 0) {
            auto next_emulated_accounts = emulated_accounts_;
            next_emulated_accounts[account_addr_] = std::make_shared<block::Account>(std::move(emulation_success.account));
            vm::Dictionary dict{trans.r1.out_msgs, 15};
            for (int ind = 0; ind < trans.outmsg_cnt; ind++) {
                auto out_msg = dict.lookup_ref(td::BitArray<15>{ind});
                if (out_msg.is_null()) {
                    set_error(td::Status::Error("Failed to lookup out_msg in emulated transaction"));
                    return;
                }

                auto message_cs = vm::load_cell_slice(out_msg);
                if (block::gen::t_CommonMsgInfo.get_tag(message_cs) == block::gen::CommonMsgInfo::ext_out_msg_info) {
                    continue;
                }
                
                auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), child_ind = pending_](td::Result<Trace *> R) {
                    if (R.is_error()) {
                        td::actor::send_closure(SelfId, &TraceEmulator::set_error, R.move_as_error());
                        return;
                    }
                    td::actor::send_closure(SelfId, &TraceEmulator::trace_emulated, R.move_as_ok(), child_ind);
                });
                td::actor::create_actor<TraceEmulator>("TraceEmulator", emulator_, shard_states_, std::move(next_emulated_accounts), std::move(out_msg), depth_ - 1, std::move(P)).release();

                pending_++;
            }
            result_->children.resize(pending_);
        } 
        if (pending_ == 0) {
            promise_.set_value(std::move(result_));
            stop();
        }
    }

    void trace_emulated(Trace *trace, size_t ind) {
        result_->children[ind] = trace;
        if (--pending_ == 0) {
            promise_.set_value(std::move(result_));
            stop();
        }
    }
};

class McBlockEmulator: public td::actor::Actor {
    MasterchainBlockDataState mc_data_state_;
    td::Promise<> promise_;
    size_t blocks_left_to_parse_;
    std::vector<TransactionInfo> txs_;

    std::shared_ptr<emulator::TransactionEmulator> emulator_;
    std::vector<td::Ref<vm::Cell>> shard_states_;

    // td::actor::ActorOwn<RedisDB> redis_db_;

    std::unordered_map<td::Bits256, std::reference_wrapper<const TransactionInfo>, BitArrayHasher> tx_by_in_msg_hash_;

    std::unordered_set<TraceId, BitArrayHasher> trace_ids_in_progress_;

    int traces_cnt_;

    td::Timestamp start_time_;

    // static map for matching in-out msgs between blocks to store trace ids. TODO: clean up old entries.
    inline static std::unordered_map<td::Bits256, TraceId, BitArrayHasher> interblock_trace_ids_;

public:
    McBlockEmulator(MasterchainBlockDataState mc_data_state, td::Promise<> promise)
        : mc_data_state_(std::move(mc_data_state)), promise_(std::move(promise)), blocks_left_to_parse_(mc_data_state_.shard_blocks_diff_.size()) {
            auto libraries_root = mc_data_state_.config_->get_libraries_root();
            emulator_ = std::make_shared<emulator::TransactionEmulator>(std::move(*mc_data_state_.config_), 0);
            emulator_->set_libs(vm::Dictionary(libraries_root, 256));
    }

    void start_up() override {
        start_time_ = td::Timestamp::now();
        for (const auto& shard_state : mc_data_state_.shard_blocks_) {
            shard_states_.push_back(shard_state.block_state);
        }

        for (auto& block_data : mc_data_state_.shard_blocks_diff_) {
            LOG(INFO) << "Parsing block " << block_data.block_data->block_id().to_str();
            auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), blk_id = block_data.block_data->block_id().id](td::Result<std::vector<TransactionInfo>> R) {
                if (R.is_error()) {
                    td::actor::send_closure(SelfId, &McBlockEmulator::parse_error, blk_id, R.move_as_error());
                    return;
                }
                td::actor::send_closure(SelfId, &McBlockEmulator::block_parsed, blk_id, R.move_as_ok());
            });
            td::actor::create_actor<BlockParser>("BlockParser", block_data.block_data, std::move(P)).release();
        }
    }

    void parse_error(ton::BlockId blkid, td::Status error) {
        LOG(ERROR) << "Failed to parse block " << blkid.to_str() << ": " << error;
        promise_.set_error(std::move(error));
        stop();
    }

    void block_parsed(ton::BlockId blkid, std::vector<TransactionInfo> txs) {
        txs_.insert(txs_.end(), txs.begin(), txs.end());
        blocks_left_to_parse_--;
        if (blocks_left_to_parse_ == 0) {
            process_txs();
        }
    }

    void process_txs() {
        std::sort(txs_.begin(), txs_.end(), [](const TransactionInfo& a, const TransactionInfo& b) {
            return a.lt < b.lt;
        });

        std::unordered_map<td::Bits256, TransactionInfo, BitArrayHasher> txs_by_out_msg_hash;
        for (const auto& tx : txs_) {
            tx_by_in_msg_hash_.insert({tx.in_msg_hash, std::ref(tx)});
            for (const auto& out_msg : tx.out_msgs) {
                txs_by_out_msg_hash.insert({out_msg.hash, tx});
            }
        }

        for (auto& tx : txs_) {
            if (tx.is_first) {
                tx.initial_msg_hash = tx.in_msg_hash;
            } else if (txs_by_out_msg_hash.find(tx.in_msg_hash) != txs_by_out_msg_hash.end() && txs_by_out_msg_hash[tx.in_msg_hash].initial_msg_hash.has_value()) {
                tx.initial_msg_hash = txs_by_out_msg_hash[tx.in_msg_hash].initial_msg_hash;
            } else if (interblock_trace_ids_.find(tx.in_msg_hash) != interblock_trace_ids_.end()) {
                tx.initial_msg_hash = interblock_trace_ids_[tx.in_msg_hash];
            } else {
                LOG(WARNING) << "Couldn't get initial_msg_hash for tx " << tx.hash.to_hex() << ". This tx will be skipped.";
            }

            // write trace_id for out_msgs for interblock chains
            if (tx.initial_msg_hash.has_value()) {
                for (const auto& out_msg : tx.out_msgs) {
                    interblock_trace_ids_[out_msg.hash] = tx.initial_msg_hash.value();
                }
            }
        }

        emulate_traces();
    }

    void db_error(td::Status error) {
        LOG(ERROR) << "Failed to lookup trace_ids: " << error;
        promise_.set_error(std::move(error));
        stop();
    }

    void emulate_traces() {
        for (auto& tx : txs_) {
            if (!tx.initial_msg_hash.has_value()) {
                // we don't emulate traces for transactions that have no initial_msg_hash
                continue;
            }
            if (trace_ids_in_progress_.find(tx.initial_msg_hash.value()) != trace_ids_in_progress_.end()) {
                // we already emulating trace for this trace_id
                continue;
            }

            // if (tx.hash.to_hex() != "36F63618F374305C85AAEF856E4EE055540FA6F199453F8F2D347C199F7FDBB2") {
            //     continue;
            // }

            // if (tx.initial_msg_hash.value().to_hex() != "A97D033AF645F4D1BE66EFFCCA441D570A1867BED666FC462A55FC620EE8EE18") {
            //     continue;
            // }
            
            auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), tx_hash = tx.hash, trace_id = tx.initial_msg_hash.value()](td::Result<Trace *> R) {
                if (R.is_error()) {
                    td::actor::send_closure(SelfId, &McBlockEmulator::trace_error, tx_hash, trace_id, R.move_as_error());
                    return;
                }

                td::actor::send_closure(SelfId, &McBlockEmulator::trace_received, tx_hash, R.move_as_ok());
            });
            td::actor::send_closure(actor_id(this), &McBlockEmulator::create_trace, tx, std::move(P));

            trace_ids_in_progress_.insert(tx.initial_msg_hash.value());
        }
    }

    void create_trace(const TransactionInfo& tx, td::Promise<Trace *> promise) {
        Trace *trace = new Trace();
        trace->emulated = false;
        trace->workchain = tx.account.workchain;
        trace->transaction_root = tx.root;
        trace->id = tx.initial_msg_hash.value();

        td::MultiPromise mp;
        auto ig = mp.init_guard();
        ig.add_promise([SelfId = actor_id(this), trace, promise = std::move(promise)](td::Result<td::Unit> R) mutable {
            if (R.is_error()) {
                promise.set_error(R.move_as_error());
                return;
            }
            promise.set_value(std::move(trace));
        });

        int children_cnt = 0;
        for (const auto out_msg : tx.out_msgs) {
            auto message_cs = vm::load_cell_slice(out_msg.root);
            if (block::gen::t_CommonMsgInfo.get_tag(message_cs) == block::gen::CommonMsgInfo::ext_out_msg_info) {
                // skip ext out msgs
                continue;
            }
            children_cnt++;
        }
        trace->children.resize(children_cnt);

        int child_ind = 0;
        for (const auto out_msg : tx.out_msgs) {
            // LOG(INFO) << "tx: " << tx.hash.to_hex() << " creating trace for msg " << out_msg.hash.to_hex();
            auto message_cs = vm::load_cell_slice(out_msg.root);
            if (block::gen::t_CommonMsgInfo.get_tag(message_cs) == block::gen::CommonMsgInfo::ext_out_msg_info) {
                // skip ext out msgs
                continue;
            }

            if (tx_by_in_msg_hash_.find(out_msg.hash) != tx_by_in_msg_hash_.end()) {
                const TransactionInfo& child_tx = tx_by_in_msg_hash_.at(out_msg.hash);
                auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), msg_hash = out_msg.hash, parent_trace = trace, child_ind, subpromise = ig.get_promise()](td::Result<Trace *> R) mutable {
                    if (R.is_error()) {
                        subpromise.set_error(R.move_as_error());
                        return;
                    }
                    parent_trace->children[child_ind] = R.move_as_ok();
                    subpromise.set_value(td::Unit());
                });
                td::actor::send_closure(actor_id(this), &McBlockEmulator::create_trace, child_tx, std::move(P));
            } else {
                // LOG(INFO) << "Emulating trace for tx " << tx.hash.to_hex() << " msg " << out_msg.hash.to_hex();
                auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), msg_hash = out_msg.hash, parent_trace = trace, child_ind, subpromise = ig.get_promise()](td::Result<Trace *> R) mutable {
                    if (R.is_error()) {
                        subpromise.set_error(R.move_as_error());
                        return;
                    }
                    parent_trace->children[child_ind] = R.move_as_ok();
                    subpromise.set_value(td::Unit());
                });
                std::unordered_map<block::StdAddress, std::shared_ptr<block::Account>, AddressHasher> shard_accounts;
                td::actor::create_actor<TraceEmulator>("TraceEmu", emulator_, shard_states_, std::move(shard_accounts), out_msg.root, 20, std::move(P)).release();
            }
            child_ind++;
        }
    }


    void trace_error(td::Bits256 tx_hash, TraceId trace_id, td::Status error) {
        LOG(ERROR) << "Failed to emulate trace_id " << trace_id.to_hex() << " from tx " << tx_hash.to_hex() << ": " << error;
        trace_ids_in_progress_.erase(trace_id);
    }

    void trace_received(td::Bits256 tx_hash, Trace *trace) {
        LOG(INFO) << "Emulated trace from tx " << tx_hash.to_hex() << ": " << trace->transactions_count() << " transactions, " << trace->depth() << " depth";
        // LOG(INFO) << trace->to_string();
        auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), trace](td::Result<td::Unit> R) {
            if (R.is_error()) {
                td::actor::send_closure(SelfId, &McBlockEmulator::trace_insert_failed, trace->id, R.move_as_error());
                return;
            }
            td::actor::send_closure(SelfId, &McBlockEmulator::trace_inserted, trace->id);
        });
        auto trace_ptr = std::shared_ptr<Trace>(trace);
        td::actor::create_actor<TraceInserter>("TraceInserter", trace_ptr, std::move(P)).release();
    }

    void trace_insert_failed(td::Bits256 trace_id, td::Status error) {
        LOG(ERROR) << "Failed to insert trace " << trace_id.to_hex() << ": " << error;
        trace_ids_in_progress_.erase(trace_id);
    }

    void trace_inserted(TraceId trace_id) {
        trace_ids_in_progress_.erase(trace_id);
        traces_cnt_++;
        if (trace_ids_in_progress_.empty()) {
            LOG(INFO) << "Finished emulating and inserting " << traces_cnt_ << " traces in " << (td::Timestamp::now().at() - start_time_.at()) * 1000 << " ms";
            promise_.set_value(td::Unit());
            stop();
        }
    }
};

void TraceEmulatorScheduler::start_up() {
    alarm_timestamp() = td::Timestamp::in(0.1);
}

void TraceEmulatorScheduler::got_last_mc_seqno(ton::BlockSeqno last_known_seqno) {
    assert(last_known_seqno >= last_known_seqno_);

    if (last_known_seqno == last_known_seqno_) {
        return;
    }
    LOG(INFO) << "New masterchain block " << last_known_seqno;
    if (last_known_seqno > last_known_seqno_ + 1) {
        LOG(WARNING) << "More than one new masterchain block appeared. Skipping to the newest one, from " << last_known_seqno_ << " to " << last_known_seqno;
    }
    last_known_seqno_ = last_known_seqno;

    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), last_known_seqno](td::Result<MasterchainBlockDataState> R) {
        if (R.is_error()) {
            td::actor::send_closure(SelfId, &TraceEmulatorScheduler::fetch_error, last_known_seqno, R.move_as_error());
            return;
        }
        auto mc_block_ds = R.move_as_ok();
        for (auto &block_ds : mc_block_ds.shard_blocks_) {
            if (block_ds.block_data->block_id().is_masterchain()) {
                mc_block_ds.config_ = block::ConfigInfo::extract_config(block_ds.block_state, block::ConfigInfo::needCapabilities | block::ConfigInfo::needLibraries | block::ConfigInfo::needWorkchainInfo | block::ConfigInfo::needSpecialSmc).move_as_ok();
                break;
            }
        }
        td::actor::send_closure(SelfId, &TraceEmulatorScheduler::seqno_fetched, last_known_seqno, std::move(mc_block_ds));
    });
    td::actor::send_closure(db_scanner_, &DbScanner::fetch_seqno, last_known_seqno, std::move(P));
}

void TraceEmulatorScheduler::fetch_error(std::uint32_t seqno, td::Status error) {
    LOG(ERROR) << "Failed to fetch seqno " << seqno << ": " << std::move(error);
}

void TraceEmulatorScheduler::seqno_fetched(std::uint32_t seqno, MasterchainBlockDataState mc_data_state) {
    LOG(DEBUG) << "Fetched seqno " << seqno;
    last_known_seqno_ = seqno;

    emulate_mc_shard(std::move(mc_data_state));
}

void TraceEmulatorScheduler::emulate_mc_shard(MasterchainBlockDataState mc_data_state) {
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), blkid = mc_data_state.shard_blocks_[0].block_data->block_id().id](td::Result<> R) {
        if (R.is_error()) {
            LOG(ERROR) << "Error emulating mc block " << blkid.to_str();
            return;
        }
        LOG(INFO) << "Success emulating mc block " << blkid.to_str();
    });
    td::actor::create_actor<McBlockEmulator>("McBlockEmulator", mc_data_state, std::move(P)).release();
}

void TraceEmulatorScheduler::parse_error(std::uint32_t seqno, td::Status error) {
    LOG(ERROR) << "Failed to parse seqno " << seqno << ": " << error;
    std::_Exit(1);
}

int seqno = 37786481;

void TraceEmulatorScheduler::alarm() {
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this)](td::Result<ton::BlockSeqno> R){
        if (R.is_error()) {
            LOG(ERROR) << "Failed to update last seqno: " << R.move_as_error();
            std::_Exit(2);
            return;
        }
        // td::actor::send_closure(SelfId, &TraceEmulatorScheduler::got_last_mc_seqno, R.move_as_ok());
        td::actor::send_closure(SelfId, &TraceEmulatorScheduler::got_last_mc_seqno, seqno++); // for debugging
    });
    td::actor::send_closure(db_scanner_, &DbScanner::get_last_mc_seqno, std::move(P));

    alarm_timestamp() = td::Timestamp::in(4.0);
}

