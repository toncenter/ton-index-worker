#pragma once
#include "td/actor/actor.h"
#include "IndexData.h"
#include "InterfaceDetectors.hpp"

class InsertManagerInterface: public td::actor::Actor {
public:
  virtual void insert(ParsedBlock block_ds, td::Promise<td::Unit> promise) = 0;

  virtual void upsert_jetton_wallet(JettonWalletData jetton_wallet, td::Promise<td::Unit> promise) = 0;
  virtual void get_jetton_wallet(std::string address, td::Promise<JettonWalletData> promise) = 0;
  virtual void upsert_jetton_master(JettonMasterData jetton_wallet, td::Promise<td::Unit> promise) = 0;
  virtual void get_jetton_master(std::string address, td::Promise<JettonMasterData> promise) = 0;
};
