#include "Scheduler.h"
#include "Sandbox.h"

namespace indexer {
void Scheduler::start_up() {
    LOG(INFO) << "Scheduler start_up";

    auto pb4 = td::actor::create_actor<MyPipelineBlock1>("mpb4", actor_id(this), PipelineBlockIdVector{}, "mpb4");
    auto pb3 = td::actor::create_actor<MyPipelineBlock1>("mpb3", actor_id(this), PipelineBlockIdVector{pb4.get()}, "mpb3");
    auto pb2 = td::actor::create_actor<MyPipelineBlock1>("mpb2", actor_id(this), PipelineBlockIdVector{pb4.get()}, "mpb2");
    auto pb1 = td::actor::create_actor<MyPipelineBlock1>("mpb1", actor_id(this), PipelineBlockIdVector{pb2.get(), pb3.get()}, "mpb1");
    pipeline_head_ = pb1.get();
    pipeline_blocks_.emplace_back(std::move(pb1));
    pipeline_blocks_.emplace_back(std::move(pb2));
    pipeline_blocks_.emplace_back(std::move(pb3));
    pipeline_blocks_.emplace_back(std::move(pb4));

    LOG(INFO) << "Alive: " << pb1.is_alive() << ", " << pb2.is_alive() << ", " << pb3.is_alive() << ", " << pb4.is_alive();

    alarm_timestamp() = td::Timestamp::in(2.0);
}

void Scheduler::alarm() {
    auto data = std::make_shared<IndexDataContainer>(last_seqno_);
    LOG(INFO) << "Scheduler alarm, seqno: " << last_seqno_;
    td::actor::send_closure(pipeline_head_, &PipelineBlock::apply, std::move(data));
    ++last_seqno_;

    alarm_timestamp() = td::Timestamp::in(2.0);
}

void Scheduler::apply(IndexDataContainerPtr data) {
    LOG(INFO) << "Scheduler::apply to " << data->seqno_ << " seqno";
}

void Scheduler::on_error(IndexDataContainerPtr data) {
    LOG(WARNING) << "Scheduler::on_error to " << data->seqno_ << " seqno";
}
}
