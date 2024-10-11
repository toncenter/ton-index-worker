#include "SandboxActor.h"

#include "validator/manager-disk.h"
#include "validator/interfaces/block.h"
#include "validator/interfaces/shard.h"

using namespace ton::validator;

void SandboxActor::start_up() {
    auto opts = ton::validator::ValidatorManagerOptions::create(
        ton::BlockIdExt{ton::masterchainId, ton::shardIdAll, 0, ton::RootHash::zero(), ton::FileHash::zero()},
        ton::BlockIdExt{ton::masterchainId, ton::shardIdAll, 0, ton::RootHash::zero(), ton::FileHash::zero()}
    );
    opts.write().set_max_open_archive_files(500);
    opts.write().set_secondary_working_dir("/tmp/sandbox-cpp");

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
            LOG(ERROR) << R.move_as_error_prefix("Failed to read last seqno:");
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
            LOG(ERROR) << R.move_as_error_prefix("Failed to read first seqno:");
            return;
        }
        this->min_seqno_ = R.move_as_ok();
        LOG(INFO) << "First seqno: " << this->min_seqno_;
    });
    td::actor::send_closure(db_, &RootDb::get_min_masterchain_seqno, std::move(P));
}

void SandboxActor::get_block(ton::BlockSeqno mc_seqno) {
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this)](td::Result<ConstBlockHandle> R) {
        if (R.is_error()) {
            LOG(ERROR) << R.move_as_error_prefix( "Failed to read block:");
            return;
        }
        td::actor::send_closure(SelfId, &SandboxActor::got_block_handle, R.move_as_ok());
    });
    td::actor::send_closure(db_, &RootDb::get_block_by_seqno,
        ton::AccountIdPrefixFull{ton::masterchainId, ton::shardIdAll}, mc_seqno, std::move(P));
}

void SandboxActor::got_block_handle(ConstBlockHandle handle) {
    // LOG(INFO) << "Got block handle " << handle->id().to_str();

    last_seqno_ = handle->id().seqno();

    td::actor::send_closure(actor_id(this), &SandboxActor::get_block, last_seqno_ - 1);
}
