#pragma once
#include <td/actor/actor.h>
#include <validator/impl/external-message.hpp>
#include <emulator/transaction-emulator.h>
#include <sw/redis++/redis++.h>
#include "IndexData.h"
#include "TraceEmulator.h"
#include "TraceInterfaceDetector.h"

struct Task {
  std::string boc;
  std::string task_id;
};

class RedisListener : public td::actor::Actor {
private:
  sw::redis::Redis redis_;
  std::string queue_name_;
  std::function<void(std::unique_ptr<Trace>, td::Promise<td::Unit>)> trace_processor_;

  std::vector<td::Ref<vm::Cell>> shard_states_;
  std::shared_ptr<emulator::TransactionEmulator> emulator_;
  std::shared_ptr<block::ConfigInfo> config_;

  std::unordered_set<td::Bits256, BitArrayHasher> known_ext_msgs_; // this set grows infinitely. TODO: remove old messages

public:
  RedisListener(std::string redis_dsn, std::string queue_name, std::function<void(std::unique_ptr<Trace>, td::Promise<td::Unit>)> trace_processor)
      : redis_(sw::redis::Redis(redis_dsn)), queue_name_(queue_name), trace_processor_(std::move(trace_processor)) {};

  virtual void start_up() override;

  void alarm() override;
  void set_mc_data_state(MasterchainBlockDataState mc_data_state);

private:
  void trace_error(TraceId trace_id, td::Status error);
  void trace_received(TraceId trace_id, Trace *trace);
  void trace_interfaces_error(TraceId trace_id, td::Status error);
  void finish_processing(std::unique_ptr<Trace> trace);
};