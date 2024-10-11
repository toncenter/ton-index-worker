#pragma once
#include "validator/db/rootdb.hpp"
#include "td/actor/actor.h"

class SandboxActor: public td::actor::Actor {
    std::string db_root_;
    td::actor::ActorOwn<ton::validator::RootDb> db_;

    ton::BlockSeqno max_seqno_ = -1, min_seqno_ = 0, last_seqno_ = 0;
public:
    SandboxActor(std::string db_root) : db_root_(std::move(db_root)) {};

    void start_up() override;
    void alarm() override;

    void get_last_block();
    void get_first_block();

    void get_block(ton::BlockSeqno mc_seqno);
    void got_block_handle(ton::validator::ConstBlockHandle handle);
};
