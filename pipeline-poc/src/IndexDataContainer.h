#pragma once
#include "vector"
#include "IndexData.h"


namespace indexer {
enum AccountStatus {
    uninit = block::gen::AccountStatus::acc_state_uninit,
    frozen = block::gen::AccountStatus::acc_state_frozen,
    active = block::gen::AccountStatus::acc_state_active,
    nonexist = block::gen::AccountStatus::acc_state_nonexist
  };

enum AccStatusChange {
    acst_unchanged = block::gen::AccStatusChange::acst_unchanged,
    acst_frozen = block::gen::AccStatusChange::acst_frozen,
    acst_deleted = block::gen::AccStatusChange::acst_deleted
};

struct BlockId {
    std::int32_t workchain;
    std::int64_t shard;
    std::uint32_t seqno;
    std::string root_hash;
    std::string file_hash;
};

struct AccountState {
    td::Bits256 hash;  // Note: hash is not unique in case account_status is "nonexist"
    block::StdAddress account;
    std::string account_friendly;  // TODO: add account friendly
    std::uint32_t timestamp;
    std::uint64_t balance;
    AccountStatus account_status;
    std::optional<td::Bits256> frozen_hash;
    td::Ref<vm::Cell> code;
    std::optional<td::Bits256> code_hash;
    td::Ref<vm::Cell> data;
    std::optional<td::Bits256> data_hash;
    td::Bits256 last_trans_hash;
    std::uint64_t last_trans_lt;  // in "nonexist" case it is lt of block, not tx. TODO: fix it

    bool operator==(const AccountState& other) const {
        return hash == other.hash && account == other.account && timestamp == other.timestamp;
    }
};


struct Block {
    BlockId id;
    std::optional<BlockId> mc_block;

    std::int32_t global_id;
    std::int32_t version;
    bool after_merge;
    bool before_split;
    bool after_split;
    bool want_merge;
    bool want_split;
    bool key_block;
    bool vert_seqno_incr;
    std::int32_t flags;
    std::int32_t gen_utime;
    std::uint64_t start_lt;
    std::uint64_t end_lt;
    std::int32_t validator_list_hash_short;
    std::int32_t gen_catchain_seqno;
    std::int32_t min_ref_mc_seqno;
    std::int32_t prev_key_block_seqno;
    std::int32_t vert_seqno;
    std::optional<int32_t> master_ref_seqno;
    std::string rand_seed;
    std::string created_by;

    // std::vector<Transaction> transactions;
    std::vector<BlockId> prev_blocks;
};


struct IndexDataContainer {
    using State = std::uint64_t;
    const State STAGE_1 = 1;
    const State STAGE_2 = 1 << 2;
    const State STAGE_3 = 1 << 3;

    std::int32_t seqno_{0};
    State state_{0};

    std::vector<schema::Block> blocks_;
    std::vector<schema::AccountState> account_states_;
    std::vector<schema::MasterchainBlockShard> masterchain_block_shards_;

    explicit IndexDataContainer(std::int32_t seqno) : seqno_(seqno) {}
};

using IndexDataContainerPtr = std::shared_ptr<IndexDataContainer>;
}