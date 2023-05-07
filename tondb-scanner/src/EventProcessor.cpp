#include "EventProcessor.h"
#include "td/actor/actor.h"
#include "vm/cells/Cell.h"
#include "vm/stack.hpp"
#include "common/refcnt.hpp"
#include "smc-envelope/SmartContract.h"
#include "crypto/block/block-auto.h"
#include "td/utils/base64.h"
#include "IndexData.h"
#include "td/actor/MultiPromise.h"
#include "convert-utils.h"


// process ParsedBlock and try detect master and wallet interfaces
void EventProcessor::process(ParsedBlock block, td::Promise<td::Unit> &&promise) {
  LOG(INFO) << "Detecting interfaces " << block.account_states_.size();
  td::MultiPromise mp;
  auto ig = mp.init_guard();
  ig.add_promise(std::move(promise));
  for (auto& account_state : block.account_states_) {
    LOG(INFO) << "Detecting interfaces for " << account_state.account;
    auto address_res = block::StdAddress::parse(account_state.account);
    if (address_res.is_error()) {
      continue;
    }
    auto address = address_res.move_as_ok();
    
    auto code_cell = account_state.code;
    auto data_cell = account_state.data; 
    if (code_cell.is_null() || data_cell.is_null()) {
      continue; //TODO: Should continue when data is null?
    }
    auto last_tx_lt = account_state.last_trans_lt;
    auto P1 = td::PromiseCreator::lambda([this, code_cell, address, promise = ig.get_promise()](td::Result<JettonMasterData> master_data) mutable {
      bool is_master = master_data.is_ok();
      if (!is_master) {
        LOG(INFO) << "Failed to detect interface JETTON_MASTER for " << convert::to_raw_address(address) << ": " << master_data.move_as_error();
      } else {
        LOG(INFO) << "Detected interface JETTON_MASTER for " << convert::to_raw_address(address);
      }
      td::actor::send_closure(interface_manager_, &InterfaceManager::set_interface, code_cell->get_hash(), IT_JETTON_MASTER, is_master, std::move(promise));
    });
    td::actor::send_closure(jetton_master_detector_, &JettonMasterDetector::detect, address, code_cell, data_cell, last_tx_lt, std::move(P1));

    auto P2 = td::PromiseCreator::lambda([this, code_cell, address, promise = ig.get_promise()](td::Result<JettonWalletData> wallet_data) mutable {
      bool is_wallet = wallet_data.is_ok();
      if (!is_wallet) {
        LOG(INFO) << "Failed to detect interface JETTON_WALLET for " << convert::to_raw_address(address) << ": " << wallet_data.move_as_error();
      } else {
        LOG(INFO) << "Detected interface JETTON_WALLET for " << convert::to_raw_address(address);
      }
      td::actor::send_closure(interface_manager_, &InterfaceManager::set_interface, code_cell->get_hash(), IT_JETTON_WALLET, is_wallet, std::move(promise));
    });
    td::actor::send_closure(jetton_wallet_detector_, &JettonWalletDetector::detect, address, code_cell, data_cell, last_tx_lt, std::move(P2));
  }
}

void EventProcessor::process_transactions(ParsedBlock block, td::Promise<td::Unit> &&promise) {
  LOG(INFO) << "Detecting tokens transactions " << block.transactions_.size();
  td::MultiPromise mp;
  auto ig = mp.init_guard();
  ig.add_promise(std::move(promise));

  for (auto& tx : block.transactions_) {
    if (!tx.compute_exit_code || !tx.compute_exit_code.value()) {
      // tx is not successfull, skipping
      continue;
    }
    if (tx.in_msg_body.is_null()) {
      // tx doesn't have in_msg, skipping
      continue;
    }
    

  }
}