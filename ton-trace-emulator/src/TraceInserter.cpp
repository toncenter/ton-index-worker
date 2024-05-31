#include "TraceInserter.h"
#include "Serializer.hpp"

void TraceInserter::start_up() {
    auto serialized_r = serialize_trace(trace_);
    if (serialized_r.is_error()) {
        promise_.set_error(serialized_r.move_as_error_prefix("Failed to serialize trace: "));
        return;
    }
    auto serialized = serialized_r.move_as_ok();

    try {
        redis_.del(trace_->id.to_hex());

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