#pragma once
#include "td/actor/actor.h"
#include "IndexData.h"

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

class InsertManagerInterface: public td::actor::Actor {
public:
  virtual void insert(ParsedBlock block_ds, td::Promise<td::Unit> promise) = 0;

  virtual void upsert_jetton_wallet(JettonWalletData jetton_wallet, td::Promise<td::Unit> promise) = 0;
  virtual void get_jetton_wallet(std::string address, td::Promise<JettonWalletData> promise) = 0;
  virtual void upsert_jetton_master(JettonMasterData jetton_wallet, td::Promise<td::Unit> promise) = 0;
  virtual void get_jetton_master(std::string address, td::Promise<JettonMasterData> promise) = 0;
};
