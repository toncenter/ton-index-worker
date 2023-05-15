#pragma once
#include <vector>
#include <variant>
#include "crypto/common/refcnt.hpp"
#include "validator/interfaces/block.h"
#include "validator/interfaces/shard.h"
#include "SchemaPostgres.h"

// struct JettonContent {

// };

struct JettonMasterData {
  std::string address;
  uint64_t total_supply;
  bool mintable;
  td::optional<std::string> admin_address;
  // JettonContent jetton_content;
  vm::CellHash jetton_wallet_code_hash;
  vm::CellHash data_hash;
  vm::CellHash code_hash;
  uint64_t last_transaction_lt;
  std::string code_boc;
  std::string data_boc;
};


struct JettonWalletData {
  uint64_t balance;
  std::string address;
  std::string owner;
  std::string jetton;
  uint64_t last_transaction_lt;
  vm::CellHash code_hash;
  vm::CellHash data_hash;
};

struct JettonTransfer {
  std::string transaction_hash;
  uint64_t query_id;
  td::RefInt256 amount;
  std::string destination;
  std::string response_destination;
  td::Ref<vm::Cell> custom_payload;
  td::RefInt256 forward_ton_amount;
  td::Ref<vm::Cell> forward_payload;
};

struct JettonBurn {
  std::string transaction_hash;
  uint64_t query_id;
  td::RefInt256 amount;
  std::string response_destination;
  td::Ref<vm::Cell> custom_payload;
};


struct BlockDataState {
  td::Ref<ton::validator::BlockData> block_data;
  td::Ref<ton::validator::ShardState> block_state;
};

using MasterchainBlockDataState = std::vector<BlockDataState>;
using BlockchainEvent = std::variant<JettonTransfer, 
                                     JettonBurn>;

struct ParsedBlock {
  MasterchainBlockDataState mc_block_;

  std::vector<schema::Block> blocks_;
  std::vector<schema::Transaction> transactions_;
  std::vector<schema::Message> messages_;
  std::vector<schema::TransactionMessage> transaction_messages_;
  std::vector<schema::AccountState> account_states_;

  std::vector<BlockchainEvent> events_;
  template <class T>
  std::vector<T> get_events() {
    std::vector<T> result;
    for (auto& event: events_) {
      if (std::holds_alternative<T>(event)) {
        result.push_back(std::get<T>(event));
      }
    }
    return result;
  }
};

using ParsedBlockPtr = std::shared_ptr<ParsedBlock>;