#pragma once
#include <sw/redis++/redis++.h>
#include "td/actor/actor.h"
#include "crypto/common/bitstring.h"
#include "TraceEmulator.h"
#include "Serializer.h"



class TraceInserter: public td::actor::Actor {
public:
    std::shared_ptr<Trace> trace_;
    sw::redis::Redis redis_;
    td::Promise<td::Unit> promise_;

    TraceInserter(std::shared_ptr<Trace> trace, td::Promise<td::Unit> promise) :
        redis_(sw::redis::Redis("tcp://127.0.0.1:6379")), trace_(trace), promise_(std::move(promise)) {
        
    }

    void start_up() override {
        try {
            redis_.del(trace_->id.to_hex());

            auto serialized_r = serialize_trace(trace_);

            if (serialized_r.is_error()) {
                promise_.set_error(serialized_r.move_as_error_prefix("Failed to serialize trace: "));
                return;
            }
            auto serialized = serialized_r.move_as_ok();

            for (int i = 0; i < serialized.size(); i++) {
                std::string field;
                if (i == 0) {
                    field = "root";
                } else {
                    field = std::to_string(i);
                }
                redis_.hset(trace_->id.to_hex(), field, serialized[i]);
            }

            redis_.publish("new_trace", trace_->id.to_hex());

            promise_.set_value(td::Unit());
        } catch (const sw::redis::Error &err) {
            promise_.set_error(td::Status::Error("Redis error: " + std::string(err.what())));
        }
    }
};