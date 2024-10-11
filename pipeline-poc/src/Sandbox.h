#pragma once
#include "PipelineBlock.h"

namespace indexer {
class MyPipelineBlock1: public PipelineBlock {
    PipelineBlockId parent_;
    PipelineBlockIdVector children_;

    std::string name_;
    std::int32_t counter_{0};
public:
    MyPipelineBlock1(PipelineBlockId parent, PipelineBlockIdVector children, std::string name) :
        parent_(std::move(parent)), children_(std::move(children)), name_(std::move(name)) {
        LOG(INFO) << "MyPipelineBlock1 with name " << name_ << " created and has " << children_.size()
            << " children and parent_.is_alive: " << parent_.is_alive();
    }
    void apply(IndexDataContainerPtr data) override;
    void on_error(IndexDataContainerPtr data) override;
};
}
