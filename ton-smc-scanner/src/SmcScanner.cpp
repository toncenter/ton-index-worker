#include "SmcScanner.h"
#include "convert-utils.h"

void SmcScanner::start_up() {
    if (!options_.cur_addr.is_zero()) {
        td::actor::send_closure(actor_id(this), &SmcScanner::got_checkpoint, options_.cur_addr);
    } else if (options_.from_checkpoint) {
        auto P = td::PromiseCreator::lambda([=, SelfId = actor_id(this)](td::Result<td::Bits256> R) {
            td::Bits256 cur_addr{td::Bits256::zero()};
            if (R.is_error()) {
                LOG(ERROR) << "Failed to restore checkpoint: " << R.move_as_error();
            } else {
                cur_addr = R.move_as_ok();
            }
            td::actor::send_closure(SelfId, &SmcScanner::got_checkpoint, std::move(cur_addr));
        });

        td::actor::send_closure(options_.insert_manager_, &PostgreSQLInsertManager::checkpoint_read, std::move(P));
    } else {
        td::actor::send_closure(actor_id(this), &SmcScanner::got_checkpoint, td::Bits256::zero());
    }
}

void SmcScanner::got_checkpoint(td::Bits256 cur_addr) {
    LOG(INFO) << "Restored checkpoint address: " << cur_addr;
    options_.cur_addr = cur_addr;

    auto P = td::PromiseCreator::lambda([=, SelfId = actor_id(this)](td::Result<MasterchainBlockDataState> R){
        if (R.is_error()) {
            LOG(ERROR) << "Failed to get seqno " << options_.seqno_ << ": " << R.move_as_error();
            stop();
            return;
        }
        td::actor::send_closure(SelfId, &SmcScanner::got_block, R.move_as_ok());
    });

    td::actor::send_closure(db_scanner_, &DbScanner::fetch_seqno, options_.seqno_, std::move(P));
}

void SmcScanner::got_block(MasterchainBlockDataState block) {
    LOG(INFO) << "Got block data state";
    for (const auto &shard_ds : block.shard_blocks_) {
        auto& shard_state = shard_ds.block_state;
        td::actor::create_actor<ShardStateScanner>("ShardStateScanner", shard_state, block, options_).release();
    }
}

ShardStateScanner::ShardStateScanner(td::Ref<vm::Cell> shard_state, MasterchainBlockDataState mc_block_ds, Options options) 
    : shard_state_(shard_state), mc_block_ds_(mc_block_ds), options_(options) {
    LOG(INFO) << "Created ShardStateScanner!";
}

void ShardStateScanner::schedule_next() {
    block::gen::ShardStateUnsplit::Record sstate_;
    if (!tlb::unpack_cell(shard_state_, sstate_)) {
        LOG(ERROR) << "Failed to unpack ShardStateUnsplit";
        stop();
        return;
    }
    
    vm::AugmentedDictionary accounts_dict{vm::load_cell_slice_ref(sstate_.accounts), 256, block::tlb::aug_ShardAccounts};

    int count = 0;
    allow_same = true;
    while (!finished && in_progress_.load() < 1000) {
        auto shard_account_csr = accounts_dict.vm::DictionaryFixed::lookup_nearest_key(cur_addr_.bits(), 256, true, allow_same);
        if (shard_account_csr.is_null()) {
            finished = true;
            break;
        }
        allow_same = false;

        queue_.push_back(std::make_pair(cur_addr_, std::move(shard_account_csr)));
        if (queue_.size() > options_.batch_size_) {
            // LOG(INFO) << "Dispatched batch of " << queue_.size() << " account states";
            std::vector<std::pair<td::Bits256, td::Ref<vm::CellSlice>>> batch_;
            std::copy(queue_.begin(), queue_.end(), std::back_inserter(batch_));
            queue_.clear();
            
            in_progress_.fetch_add(1);
            td::actor::create_actor<StateBatchParser>("parser", std::move(batch_), shard_state_data_, actor_id(this), options_).release();
        }
    }
    processed_ += count;

    if(!finished) {
        // td::actor::send_closure(options_.insert_manager_, &PostgreSQLInsertManager::checkpoint, cur_addr_);
        alarm_timestamp() = td::Timestamp::in(1.0);
    } else {
        LOG(ERROR) << "Finished!";
        stop();
    }
}

void ShardStateScanner::start_up() {
    // cur_addr_.from_hex("012508807D259B1F3BDD2A830CF7F4591838E0A1D1474A476B20CFB540CD465B");
    // cur_addr_.from_hex("E750CF93EAEDD2EC01B5DE8F49A334622BD630A8728806ABA65F1443EB7C8FD7");

    cur_addr_ = options_.cur_addr;
    shard_state_data_ = std::make_shared<ShardStateData>();
    for (const auto &shard_ds : mc_block_ds_.shard_blocks_) {
        shard_state_data_->shard_states_.push_back(shard_ds.block_state);
    }
    auto config_r = block::ConfigInfo::extract_config(mc_block_ds_.shard_blocks_[0].block_state, block::ConfigInfo::needCapabilities | block::ConfigInfo::needLibraries);
    if (config_r.is_error()) {
        LOG(ERROR) << "Failed to extract config: " << config_r.move_as_error();
        std::_Exit(2);
        return;
    }
    shard_state_data_->config_ = config_r.move_as_ok();

    alarm_timestamp() = td::Timestamp::in(0.1);
}

void ShardStateScanner::alarm() {
    LOG(INFO) << "cur_addr: " << td::base64_encode(cur_addr_.as_slice());
    schedule_next();
}

void ShardStateScanner::batch_inserted() {
    in_progress_.fetch_sub(1);
}

void StateBatchParser::interfaces_detected(std::vector<Detector::DetectedInterface> ifaces) {
    LOG(INFO) << "Detected, but not queued!";
}

void StateBatchParser::process_account_states(std::vector<schema::AccountState> account_states) {
    // LOG(INFO) << "Processing account state " << account.account;
    for (auto &account : account_states) {
        auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), addr = account.account](td::Result<std::vector<Detector::DetectedInterface>> R) {
            if (R.is_error()) {
                LOG(ERROR) << "Failed to detect interfaces of account '" << addr << "'";
                return;
            }
            td::actor::send_closure(SelfId, &StateBatchParser::interfaces_detected, R.move_as_ok());
        });
        td::actor::create_actor<Detector>("InterfacesDetector", account.account, account.code, account.data, shard_state_data_->shard_states_, shard_state_data_->config_, std::move(P)).release();
    }
}

void StateBatchParser::start_up() {
    // if (cur_addr_.to_hex() != "E753CF93EAEDD2EC01B5DE8F49A334622BD630A8728806ABA65F1443EB7C8FD7") {
    //     continue;
    // }
    std::vector<schema::AccountState> state_list_;
    for (auto &[addr_, shard_account_csr] : data_) {
        td::Ref<vm::Cell> account_root = shard_account_csr->prefetch_ref();
        int account_tag = block::gen::t_Account.get_tag(vm::load_cell_slice(account_root));
        switch (account_tag) {
            case block::gen::Account::account_none: break;
            case block::gen::Account::account: {
                auto account_r = ParseQuery::parse_account(account_root, shard_state_data_->sstate_.gen_utime);
                if (account_r.is_error()) {
                    LOG(ERROR) << "Failed to parse account " << addr_.to_hex() << ": " << account_r.move_as_error();
                    break;
                }
                auto account_state_ = account_r.move_as_ok();
                result_.push_back(account_state_);
                state_list_.push_back(account_state_);
                break;
            }
            default: LOG(ERROR) << "Unknown account tag"; break;
        }
    }
    if (options_.index_interfaces_) {
        process_account_states(state_list_);
    } else {
        std::copy(state_list_.begin(), state_list_.end(), std::back_inserter(result_));
        processing_finished();
    }
}

void StateBatchParser::processing_finished() {
    td::actor::send_closure(options_.insert_manager_, &PostgreSQLInsertManager::insert_data, std::move(result_));
    td::actor::send_closure(shard_state_scanner_, &ShardStateScanner::batch_inserted);
}
