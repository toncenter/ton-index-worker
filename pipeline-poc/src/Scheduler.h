#pragma once
#include "td/actor/actor.h"
#include "IndexOptions.h"
#include "PipelineBlock.h"


namespace indexer {
class Scheduler: public PipelineBlock {
    IndexOptions opts_;
    PipelineBlockOwnVector pipeline_blocks_;
    PipelineBlockId pipeline_head_;
    std::int32_t last_seqno_{0};
public:
    explicit Scheduler(IndexOptions opts) : opts_(std::move(opts)) {}
    void start_up() override;
    void alarm() override;

    void apply(IndexDataContainerPtr data) override;
    void on_error(IndexDataContainerPtr data) override;
};
}
