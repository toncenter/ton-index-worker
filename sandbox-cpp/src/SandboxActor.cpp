#include "SandboxActor.h"

#include <func/func.h>

#include "validator/manager-disk.h"
#include "validator/interfaces/block.h"
#include "validator/interfaces/shard.h"

using namespace ton::validator;

struct BlockInfo {
    ton::BlockSeqno mc_seqno;
    td::Ref<BlockData> data;
    td::Ref<vm::Cell> state;
};


class BlockReadQuery: public td::actor::Actor {
    td::actor::ActorId<RootDb> db_;
    ton::BlockSeqno mc_seqno_;
    ConstBlockHandle handle_;
    td::Promise<BlockInfo> promise_;

    td::Ref<BlockData> data_;
    td::Ref<vm::Cell> state_;
public:
    explicit BlockReadQuery(td::actor::ActorId<RootDb> db, const ton::BlockSeqno& mc_seqno, ConstBlockHandle handle, td::Promise<BlockInfo> promise) :
        db_(std::move(db)), mc_seqno_(mc_seqno), handle_(std::move(handle)), promise_(std::move(promise)) {}

    void start_up() override {
        auto P = td::PromiseCreator::lambda([SelfId = actor_id(this)](td::Result<td::Ref<BlockData>> R) {
              td::actor::send_closure(SelfId, &BlockReadQuery::got_block_data, std::move(R));
            });
        td::actor::send_closure(db_, &RootDb::get_block_data, handle_, std::move(P));
    }
    void got_block_data(td::Result<td::Ref<BlockData>> res) {
        if (res.is_error()) {
            promise_.set_error(res.move_as_error_prefix("failed to read block data:"));
            stop();
            return;
        }
        data_ = res.move_as_ok();
        auto P = td::PromiseCreator::lambda([SelfId = actor_id(this)](td::Result<td::Ref<vm::Cell>> R) {
          td::actor::send_closure(SelfId, &BlockReadQuery::got_block_state, std::move(R));
        });
        td::actor::send_closure(db_, &RootDb::get_block_state_root, handle_, std::move(P));
    }

    void got_block_state(td::Result<td::Ref<vm::Cell>> res) {
        if (res.is_error()) {
            promise_.set_error(res.move_as_error_prefix("failed to read block state:"));
            stop();
            return;
        }
        state_ = res.move_as_ok();

        if (data_.is_null()) {
            promise_.set_error(td::Status::Error("data is null"));
        } else if (state_.is_null()) {
            promise_.set_error(td::Status::Error("state is null"));
        } else {
            promise_.set_value(BlockInfo{mc_seqno_, std::move(data_), std::move(state_)});
        }
        stop();
    }
};


class MasterchainBlockReadQuery: public td::actor::Actor {
    td::actor::ActorId<RootDb> db_;
    ton::BlockSeqno seqno_;
    td::Promise<bool> promise_;

    BlockInfo mc_block_info_;
public:
    explicit MasterchainBlockReadQuery(td::actor::ActorId<RootDb> db, const ton::BlockSeqno& seqno, td::Promise<bool> promise):
    db_(std::move(db)), seqno_(seqno), promise_(std::move(promise)) {}

    void start_up() override {
        auto P = td::PromiseCreator::lambda([SelfId = actor_id(this)](td::Result<ConstBlockHandle> R) {
            td::actor::send_closure(SelfId, &MasterchainBlockReadQuery::get_mc_block_data, std::move(R));
        });
        td::actor::send_closure(db_, &RootDb::get_block_by_seqno,
                ton::AccountIdPrefixFull{ton::masterchainId, ton::shardIdAll}, seqno_, std::move(P));
    }

    void get_mc_block_data(td::Result<ConstBlockHandle> res) {
        if (res.is_error()) {
            promise_.set_error(res.move_as_error_prefix("failed to read masterchain block by seqno: "));
            stop();
            return;
        }
        auto handle = res.move_as_ok();
        auto P = td::PromiseCreator::lambda([this, SelfId = actor_id(this)](td::Result<BlockInfo> R) {
            td::actor::send_closure(SelfId, &MasterchainBlockReadQuery::got_mc_block_info, std::move(R));
        });
        td::actor::create_actor<BlockReadQuery>("block_query", db_, seqno_, std::move(handle), std::move(P)).release();
    }

    void got_mc_block_info(td::Result<BlockInfo> res) {
        if (res.is_error()) {
            promise_.set_error(res.move_as_error_prefix("failed to get masterchain blockinfo: "));
            stop();
            return;
        }
        mc_block_info_ = res.move_as_ok();
        LOG(INFO) << "Masterchain block " << mc_block_info_.mc_seqno << " data read";
        promise_.set_value(true);
        stop();
    }
};

void SandboxActor::start_up() {
    auto opts = ton::validator::ValidatorManagerOptions::create(
        ton::BlockIdExt{ton::masterchainId, ton::shardIdAll, 0, ton::RootHash::zero(), ton::FileHash::zero()},
        ton::BlockIdExt{ton::masterchainId, ton::shardIdAll, 0, ton::RootHash::zero(), ton::FileHash::zero()}
    );
    opts.write().set_max_open_archive_files(500);
    opts.write().set_secondary_working_dir("/tmp/sandbox-cpp");
    opts.write().set_celldb_compress_depth(5);

    db_ = td::actor::create_actor<ton::validator::RootDb>("db", td::actor::ActorId<ton::validator::ValidatorManager>(),
        db_root_,  std::move(opts), td::DbOpenMode::db_secondary);

    get_last_block();
    get_first_block();
    alarm_timestamp() = td::Timestamp::in(1.0);
}

void SandboxActor::alarm() {
    LOG(INFO) << "Seqno read: " << last_seqno_;
    alarm_timestamp() = td::Timestamp::in(1.0);
}

void SandboxActor::get_last_block() {
    auto P = td::PromiseCreator::lambda([this, SelfId = actor_id(this)](td::Result<ton::BlockSeqno> R) {
        if (R.is_error()) {
            LOG(ERROR) << R.move_as_error_prefix("Failed to read last seqno: ");
            return;
        }
        this->max_seqno_ = R.move_as_ok();
        LOG(INFO) << "Last seqno: " << this->max_seqno_;

        td::actor::send_closure(SelfId, &SandboxActor::get_block, this->max_seqno_);
    });
    td::actor::send_closure(db_, &RootDb::get_max_masterchain_seqno, std::move(P));
}

void SandboxActor::get_first_block() {
    auto P = td::PromiseCreator::lambda([this](td::Result<ton::BlockSeqno> R) {
        if (R.is_error()) {
            LOG(ERROR) << R.move_as_error_prefix("Failed to read first seqno: ");
            return;
        }
        this->min_seqno_ = R.move_as_ok();
        LOG(INFO) << "First seqno: " << this->min_seqno_;
    });
    td::actor::send_closure(db_, &RootDb::get_min_masterchain_seqno, std::move(P));
}

void SandboxActor::get_block(ton::BlockSeqno mc_seqno) {
    auto P = td::PromiseCreator::lambda([mc_seqno, SelfId=actor_id(this)](td::Result<bool> R) {
        if (R.is_error()) {
            LOG(ERROR) << "Seqno: " << mc_seqno << " Error: " << R.move_as_error();
            return;
        }
        td::actor::send_closure(SelfId, &SandboxActor::get_block, mc_seqno - 1);
    });
    td::actor::create_actor<MasterchainBlockReadQuery>("query", db_.get(), mc_seqno, std::move(P)).release();
}

void SandboxActor::got_block(bool result) {
}
