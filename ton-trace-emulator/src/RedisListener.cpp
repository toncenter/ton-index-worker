#include "RedisListener.h"


void RedisListener::start_up() {
  alarm_timestamp() = td::Timestamp::now() ;
}

void RedisListener::alarm() {
  if (emulator_ == nullptr) {
    alarm_timestamp() = td::Timestamp::in(0.1);
    return;
  }

  while (auto value = redis_.rpop(queue_name_)) {
    auto boc_decoded = td::base64_decode(td::Slice(value.value()));
    if (boc_decoded.is_error()) {
      LOG(ERROR) << "Can't decode base64 boc: " << boc_decoded.move_as_error();
      continue;
    }
    auto msg_cell_r = vm::std_boc_deserialize(boc_decoded.move_as_ok());
    if (msg_cell_r.is_error()) {
      LOG(ERROR) << "Can't deserialize message boc: " << msg_cell_r.move_as_error();
      continue;
    }
    auto msg_cell = msg_cell_r.move_as_ok();

    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), msg_hash = td::Bits256(msg_cell->get_hash().bits())](td::Result<Trace *> R) mutable {
      if (R.is_error()) {
        td::actor::send_closure(SelfId, &RedisListener::trace_error, msg_hash, R.move_as_error());
      } else {
        td::actor::send_closure(SelfId, &RedisListener::trace_received, msg_hash, R.move_as_ok());
      }
    });
    std::unordered_map<block::StdAddress, std::shared_ptr<block::Account>, AddressHasher> shard_accounts;
    td::actor::create_actor<TraceEmulator>("TraceEmu", emulator_, shard_states_, shard_accounts, msg_cell, 20, std::move(P)).release();
  }

  alarm_timestamp() = td::Timestamp::in(0.1);
}

void RedisListener::set_mc_data_state(MasterchainBlockDataState mc_data_state) {
  for (const auto& shard_state : mc_data_state.shard_blocks_) {
      shard_states_.push_back(shard_state.block_state);
  }

  auto libraries_root = mc_data_state.config_->get_libraries_root();
  emulator_ = std::make_shared<emulator::TransactionEmulator>(mc_data_state.config_, 0);
  emulator_->set_libs(vm::Dictionary(libraries_root, 256));

  config_ = mc_data_state.config_;
}

void RedisListener::trace_error(TraceId trace_id, td::Status error) {
  LOG(ERROR) << "Failed to emulate trace_id " << trace_id.to_hex() << ": " << error;
  known_ext_msgs_.erase(trace_id);
}

void RedisListener::trace_received(TraceId trace_id, Trace *trace) {
  LOG(INFO) << "Emulated trace from ext msg " << trace_id.to_hex() << ": " << trace->transactions_count() << " transactions, " << trace->depth() << " depth";
  if constexpr (std::variant_size_v<Trace::Detector::DetectedInterface> > 0) {
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), trace_id = trace->id](td::Result<std::unique_ptr<Trace>> R) {
      if (R.is_error()) {
        td::actor::send_closure(SelfId, &RedisListener::trace_interfaces_error, trace_id, R.move_as_error());
        return;
      }
      td::actor::send_closure(SelfId, &RedisListener::finish_processing, R.move_as_ok());
    });

    td::actor::create_actor<TraceInterfaceDetector>("TraceInterfaceDetector", shard_states_, config_, std::unique_ptr<Trace>(trace), std::move(P)).release();
  } else {
    finish_processing(std::unique_ptr<Trace>(trace));
  }
}

void RedisListener::trace_interfaces_error(TraceId trace_id, td::Status error) {
    LOG(ERROR) << "Failed to detect interfaces on trace_id " << trace_id.to_hex() << ": " << error;
}

void RedisListener::finish_processing(std::unique_ptr<Trace> trace) {
    LOG(INFO) << "Finished emulating trace " << trace->id.to_hex();
    trace_processor_(std::move(trace));
}