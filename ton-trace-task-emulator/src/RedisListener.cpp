#include <msgpack.hpp>
#include "RedisListener.h"


struct TraceTask {
  std::string id;
  std::string boc;

  MSGPACK_DEFINE(id, boc);
};

void RedisListener::start_up() {
  alarm_timestamp() = td::Timestamp::now() ;
}

void RedisListener::alarm() {
  while (auto buffer = redis_.rpop(queue_name_)) {
    TraceTask task;
    try {
        size_t offset = 0;
        msgpack::unpacked res;
        msgpack::unpack(res, buffer.value().data(), buffer.value().size(), offset);
        msgpack::object obj = res.get();

        obj.convert(task);
    } catch (const std::exception &e) {
        LOG(ERROR) << "Failed to unpack trace task: " << e.what();
        continue;
    }

    auto boc_decoded = td::base64_decode(task.boc);
    if (boc_decoded.is_error()) {
      auto error = td::Status::Error(PSLICE() << "Can't decode base64 boc: " << boc_decoded.move_as_error());
      LOG(ERROR) << error;
      TraceEmulationResult res{task.id, std::move(error), current_mc_block_id_};
      trace_processor_(std::move(res), td::PromiseCreator::lambda([](td::Result<td::Unit> R) {}));
      continue;
    }
    auto msg_cell_r = vm::std_boc_deserialize(boc_decoded.move_as_ok());
    if (msg_cell_r.is_error()) {
      auto error = td::Status::Error(PSLICE() << "Can't deserialize message boc: " << msg_cell_r.move_as_error());
      LOG(ERROR) << error;
      TraceEmulationResult res{task.id, std::move(error), current_mc_block_id_};
      trace_processor_(std::move(res), td::PromiseCreator::lambda([](td::Result<td::Unit> R) {}));
      continue;
    }
    auto msg_cell = msg_cell_r.move_as_ok();

    if (mc_data_state_.config_ == nullptr) {
      trace_error(task.id, current_mc_block_id_, td::Status::Error("RedisListener not ready"));
      alarm_timestamp() = td::Timestamp::in(0.1);
      return;
    }

    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), task_id = std::move(task.id), mc_blkid = current_mc_block_id_](td::Result<Trace> R) mutable {
      if (R.is_error()) {
        td::actor::send_closure(SelfId, &RedisListener::trace_error, std::move(task_id), std::move(mc_blkid), R.move_as_error());
      } else {
        td::actor::send_closure(SelfId, &RedisListener::trace_received, std::move(task_id), std::move(mc_blkid), R.move_as_ok());
      }
    });
    td::actor::create_actor<TraceEmulator>("TraceEmu", mc_data_state_, msg_cell, false, std::move(P)).release();
  }

  alarm_timestamp() = td::Timestamp::now();
}

void RedisListener::set_mc_data_state(MasterchainBlockDataState mc_data_state) {
  shard_states_.clear();
  for (const auto& shard_state : mc_data_state.shard_blocks_) {
      shard_states_.push_back(shard_state.block_state);
  }

  current_mc_block_id_ = mc_data_state.shard_blocks_[0].block_data->block_id().id;

  mc_data_state_ = std::move(mc_data_state);
}

void RedisListener::trace_error(std::string task_id, ton::BlockId mc_block_id, td::Status error) {
  LOG(ERROR) << "Failed to emulate trace " << task_id << ": " << error;
  TraceEmulationResult res{task_id, error.move_as_error(), mc_block_id};
  trace_processor_(std::move(res), td::PromiseCreator::lambda([](td::Result<td::Unit> R) {}));
}

void RedisListener::trace_received(std::string task_id, ton::BlockId mc_block_id, Trace trace) {
  LOG(INFO) << "Emulated trace " << task_id << ": " << trace.transactions_count() << " transactions, " << trace.depth() << " depth";
  if constexpr (std::variant_size_v<Trace::Detector::DetectedInterface> > 0) {
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), task_id, mc_block_id = std::move(mc_block_id), trace_id = trace.id](td::Result<Trace> R) {
      if (R.is_error()) {
        td::actor::send_closure(SelfId, &RedisListener::trace_interfaces_error, std::move(task_id), std::move(mc_block_id), R.move_as_error());
        return;
      }
      td::actor::send_closure(SelfId, &RedisListener::finish_processing, std::move(task_id), std::move(mc_block_id), R.move_as_ok());
    });

    td::actor::create_actor<TraceInterfaceDetector>("TraceInterfaceDetector", shard_states_, mc_data_state_.config_, std::move(trace), std::move(P)).release();
  } else {
    finish_processing(std::move(task_id), std::move(mc_block_id), std::move(trace));
  }
}

void RedisListener::trace_interfaces_error(std::string task_id, ton::BlockId mc_block_id, td::Status error) {
    LOG(ERROR) << "Failed to detect interfaces on task " << task_id << ": " << error;
    TraceEmulationResult res{task_id, error.move_as_error(), mc_block_id};
    trace_processor_(std::move(res), td::PromiseCreator::lambda([](td::Result<td::Unit> R) {}));
}

void RedisListener::finish_processing(std::string task_id, ton::BlockId mc_block_id, Trace trace) {
    LOG(INFO) << "Finished emulating trace " << task_id;
    auto P = td::PromiseCreator::lambda([task_id](td::Result<td::Unit> R) {
      if (R.is_error()) {
        LOG(ERROR) << "Failed to insert trace task " << task_id << ": " << R.move_as_error();
        return;
      }
      LOG(DEBUG) << "Successfully inserted trace task" << task_id;
    });
    TraceEmulationResult res{task_id, std::move(trace), mc_block_id};
    trace_processor_(std::move(res), std::move(P));
}