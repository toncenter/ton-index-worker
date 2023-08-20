#pragma once
#include <queue>
#include "td/actor/actor.h"


class IndexScheduler: public td::actor::Actor {
public:
    IndexScheduler() {};

    void start_up() override;
    void alarm() override;
    void run();
};
