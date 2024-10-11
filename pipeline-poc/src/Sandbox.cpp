#pragma once
#include "Sandbox.h"

namespace indexer {
void MyPipelineBlock1::apply(IndexDataContainerPtr data) {
    LOG(INFO) << name_ << "::process(" << data->seqno_ << ")";

    if (children_.empty()) {
        td::actor::send_closure(parent_, &PipelineBlock::on_error, std::move(data));
    } else {
        for(auto &child : children_) {
            td::actor::send_closure(child, &PipelineBlock::apply, data);
        }
    }
}

void MyPipelineBlock1::on_error(IndexDataContainerPtr data) {
    LOG(WARNING) << name_ << "::on_error("
              << data->seqno_ << ")";
}
}
