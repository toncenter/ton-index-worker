#pragma once
#include "IndexDataContainer.h"
#include <vector>
#include "td/actor/actor.h"

namespace indexer {
class PipelineBlock: public td::actor::Actor {
public:
    virtual void apply(IndexDataContainerPtr data) = 0;
    virtual void on_error(IndexDataContainerPtr data) = 0;
};

using PipelineBlockId = td::actor::ActorId<PipelineBlock>;
using PipelineBlockOwn = td::actor::ActorOwn<PipelineBlock>;
using PipelineBlockIdVector = std::vector<PipelineBlockId>;
using PipelineBlockOwnVector = std::vector<PipelineBlockOwn>;
}
