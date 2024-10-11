#pragma once
#include "string"

namespace indexer {
struct IndexOptions {
    std::string db_root_ = "/var/ton-work/db";
    std::string pg_dsn_ = "postgresql://localhost:5432/index";
    std::int32_t threads_{7};

    std::int32_t from_seqno_{1};
    std::int32_t to_seqno_{-1};
};
}
