#pragma once
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

enum SmcInterface {
  IT_JETTON_MASTER,
  IT_JETTON_WALLET
};

class InterfaceManager: public td::actor::Actor {
private:
  std::map<std::pair<vm::CellHash, SmcInterface>, bool> cache_{};
public:
  // Interfaces table will consist of 3 columns: code_hash, interface, has_interface

  void check_interface(vm::CellHash code_hash, SmcInterface interface, td::Promise<bool> promise) {
    if (cache_.count({code_hash, interface})) {
      promise.set_value(std::move(cache_[{code_hash, interface}]));
      return;
    }
    promise.set_error(td::Status::Error(404, "Unknown code hash"));
  }

  void set_interface(vm::CellHash code_hash, SmcInterface interface, bool has, td::Promise<td::Unit> promise) {
    cache_[{code_hash, interface}] = has;
    promise.set_value(td::Unit());
  }
};

template <typename T>
class InterfaceDetector: public td::actor::Actor {
public:
  virtual void detect(block::StdAddress address, td::Ref<vm::Cell> code_cell, td::Ref<vm::Cell> data_cell, uint64_t last_tx_lt, td::Promise<T> promise) = 0;
  virtual ~InterfaceDetector() = default;
};



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

/// @brief Detects Jetton Master according to TEP 74
/// Checks that get_jetton_data() returns (int total_supply, int mintable, slice admin_address, cell jetton_content, cell jetton_wallet_code)
class JettonMasterDetector: public InterfaceDetector<JettonMasterData> {
private:
  std::map<std::string, JettonMasterData> cache_{};
  td::actor::ActorId<InterfaceManager> interface_manager_;
public:
  JettonMasterDetector(td::actor::ActorId<InterfaceManager> interface_manager): interface_manager_(interface_manager) {
  }

  void detect(block::StdAddress address, td::Ref<vm::Cell> code_cell, td::Ref<vm::Cell> data_cell, uint64_t last_tx_lt, td::Promise<JettonMasterData> promise) override {
    auto cached_res = check_cache(address);
    if (cached_res.is_ok()) {
      auto cached_data = cached_res.move_as_ok();
      if ((data_cell->get_hash() == cached_data.data_hash && code_cell->get_hash() == cached_data.code_hash) 
          || last_tx_lt < cached_data.last_transaction_lt) {
        promise.set_value(std::move(cached_data)); // data did not not changed from cached or is more actual than requested
        return;
      }
    }

    // call interface manager and check if it is jetton master
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), address, code_cell, data_cell, last_tx_lt, promise = std::move(promise)](td::Result<bool> code_hash_is_master) mutable {
      if (code_hash_is_master.is_error()) {
        if (code_hash_is_master.error().code() == 404) { // check for code hash was not performed
          td::actor::send_closure(SelfId, &JettonMasterDetector::detect_impl, address, code_cell, data_cell, last_tx_lt, std::move(promise));
          return;
        }

        LOG(ERROR) << "Failed to get interfaces for " << convert::to_raw_address(address) << ": " << code_hash_is_master.error();
        promise.set_error(code_hash_is_master.move_as_error());
        return;
      }
      if (!code_hash_is_master.move_as_ok()) {
        promise.set_error(td::Status::Error(204, "Code hash is not a Jetton Master"));
        return;
      }

      td::actor::send_closure(SelfId, &JettonMasterDetector::detect_impl, address, code_cell, data_cell, last_tx_lt, std::move(promise));
    });

    td::actor::send_closure(interface_manager_, &InterfaceManager::check_interface, code_cell->get_hash(), IT_JETTON_MASTER, std::move(P));
  }

  void detect_impl(block::StdAddress address, td::Ref<vm::Cell> code_cell, td::Ref<vm::Cell> data_cell, uint64_t last_tx_lt,  td::Promise<JettonMasterData> promise) {
    ton::SmartContract smc({code_cell, data_cell});
    ton::SmartContract::Args args;
    args.set_now(td::Time::now());
    args.set_address(std::move(address));

    args.set_method_id("get_jetton_data");
    auto res = smc.run_get_method(args);

    const int return_stack_size = 5;
    const vm::StackEntry::Type return_types[return_stack_size] = {vm::StackEntry::Type::t_int, vm::StackEntry::Type::t_int, 
      vm::StackEntry::Type::t_slice, vm::StackEntry::Type::t_cell, vm::StackEntry::Type::t_cell};

    if (!res.success || res.stack->depth() != return_stack_size) {
      promise.set_error(td::Status::Error(204, "get_jetton_data failed"));
      return;
    }

    auto stack = res.stack->as_span();
    
    for (int i = 0; i < return_stack_size; i++) {
      if (stack[i].type() != return_types[i]) {
        promise.set_error(td::Status::Error(204, "get_jetton_data failed"));
        return;
      }
    }

    JettonMasterData data;
    data.address = convert::to_raw_address(address);
    data.total_supply = stack[0].as_int()->to_long();
    data.mintable = stack[1].as_int()->to_long() != 0;
    auto admin_address = convert::to_raw_address(stack[2].as_slice());
    if (admin_address.is_ok()) { // some jettons set unparsable address as admin address to revoke ownership (some others set it to zero address)
      data.admin_address = admin_address.move_as_ok();
    }
    data.last_transaction_lt = last_tx_lt;
    data.data_hash = data_cell->get_hash();
    data.code_boc = td::base64_encode(vm::std_boc_serialize(code_cell).move_as_ok());
    data.data_boc = td::base64_encode(vm::std_boc_serialize(data_cell).move_as_ok());
    
    // data.jetton_content = stack[3].as_cell(); todo - parse jetton content
    data.jetton_wallet_code_hash = stack[4].as_cell()->get_hash();
    
    add_to_cache(address, data);

    promise.set_value(std::move(data));
  }

  void get_wallet_address(block::StdAddress master_address, block::StdAddress owner_address, td::Promise<block::StdAddress> P) {
    auto r = check_cache(master_address);
    if (r.is_error()) {
      P.set_error(r.move_as_error());
      return;
    }
    auto data = r.move_as_ok();

    auto code_cell = vm::std_boc_deserialize(td::base64_decode(data.code_boc).move_as_ok()).move_as_ok();
    auto data_cell = vm::std_boc_deserialize(td::base64_decode(data.data_boc).move_as_ok()).move_as_ok();
    ton::SmartContract smc({code_cell, data_cell});
    ton::SmartContract::Args args;

    vm::CellBuilder anycast_cb;
    anycast_cb.store_bool_bool(false);
    auto anycast_cell = anycast_cb.finalize();
    td::Ref<vm::CellSlice> anycast_cs = vm::load_cell_slice_ref(anycast_cell);

    vm::CellBuilder cb;
    block::gen::t_MsgAddressInt.pack_addr_std(cb, anycast_cs, owner_address.workchain, owner_address.addr);
    auto owner_address_cell = cb.finalize();

    args.set_now(td::Time::now());
    args.set_address(master_address);
    args.set_stack({vm::StackEntry(vm::load_cell_slice_ref(owner_address_cell))});
    
    args.set_method_id("get_wallet_address");
    auto res = smc.run_get_method(args);

    if (!res.success || res.stack->depth() != 1) {
      P.set_error(td::Status::Error(404, "get_wallet_address failed"));
      return;
    }

    auto stack = res.stack->as_span();
    if (stack[0].type() != vm::StackEntry::Type::t_slice) {
      P.set_error(td::Status::Error(404, "get_wallet_address failed"));
      return;
    }

    auto wallet_address = convert::to_raw_address(stack[0].as_slice());
    if (wallet_address.is_error()) {
      P.set_error(wallet_address.move_as_error());
      return;
    }
    P.set_result(block::StdAddress::parse(wallet_address.move_as_ok()));
  }
private:

  td::Result<JettonMasterData> check_db(block::StdAddress address) {
    return td::Status::Error("Not implemented");
  }

  td::Result<JettonMasterData> check_cache(block::StdAddress address) {
    auto it = cache_.find(convert::to_raw_address(address));
    if (it != cache_.end()) {
      return it->second;
    }
    auto db_res = check_db(address);
    if (db_res.is_ok()) {
      cache_.emplace(convert::to_raw_address(address), db_res.move_as_ok());
      return db_res.move_as_ok();
    }
    return td::Status::Error(404, "Jetton Master not found in cache/db");
  }

  td::Status add_to_db(block::StdAddress address, JettonMasterData data) {
    return td::Status::Error("Not implemented");
  }

  td::Status add_to_cache(block::StdAddress address, JettonMasterData data) {
    cache_.emplace(convert::to_raw_address(address), data);
    
    add_to_db(address, data); // TODO: log if error

    return td::Status::OK();
  }
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

/// @brief Detects Jetton Wallet according to TEP 74
/// Checks that get_wallet_data() returns (int balance, slice owner, slice jetton, cell jetton_wallet_code)
class JettonWalletDetector: public InterfaceDetector<JettonWalletData> {
private:
  std::map<std::string, JettonWalletData> cache_{};
  td::actor::ActorId<JettonMasterDetector> jetton_master_detector_;
  td::actor::ActorId<InterfaceManager> interface_manager_;
public:
  JettonWalletDetector(td::actor::ActorId<JettonMasterDetector> jetton_master_detector,
                       td::actor::ActorId<InterfaceManager> interface_manager) 
    : jetton_master_detector_(jetton_master_detector), interface_manager_(interface_manager) {
  }

  void detect(block::StdAddress address, td::Ref<vm::Cell> code_cell, td::Ref<vm::Cell> data_cell, uint64_t last_tx_lt, td::Promise<JettonWalletData> promise) override {
    auto cached_res = check_cache(address);
    if (cached_res.is_ok()) {
      auto cached_data = cached_res.move_as_ok();
      if ((data_cell->get_hash() == cached_data.data_hash && code_cell->get_hash() == cached_data.code_hash) 
          || last_tx_lt < cached_data.last_transaction_lt) {
        promise.set_value(std::move(cached_data)); // data did not not changed from cached or cached data is more actual than requested
        return;
      }
    }

    // call interface manager and check if it is jetton wallet
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), address, code_cell, data_cell, last_tx_lt, promise = std::move(promise)](td::Result<bool> code_hash_is_wallet) mutable {
      if (code_hash_is_wallet.is_error()) {
        if (code_hash_is_wallet.error().code() == 404) { // check for code hash was not performed
          td::actor::send_closure(SelfId, &JettonWalletDetector::detect_impl, address, code_cell, data_cell, last_tx_lt, std::move(promise));
          return;
        }

        LOG(ERROR) << "Failed to get interfaces for " << convert::to_raw_address(address) << ": " << code_hash_is_wallet.error();
        promise.set_error(code_hash_is_wallet.move_as_error());
        return;
      }
      if (!code_hash_is_wallet.move_as_ok()) {
        promise.set_error(td::Status::Error(204, "Code hash is not a Jetton Wallet"));
        return;
      }

      td::actor::send_closure(SelfId, &JettonWalletDetector::detect_impl, address, code_cell, data_cell, last_tx_lt, std::move(promise));
    });

    td::actor::send_closure(interface_manager_, &InterfaceManager::check_interface, code_cell->get_hash(), IT_JETTON_WALLET, std::move(P));
  }

  void detect_impl(block::StdAddress address, td::Ref<vm::Cell> code_cell, td::Ref<vm::Cell> data_cell, uint64_t last_tx_lt,  td::Promise<JettonWalletData> promise) {
    ton::SmartContract smc({code_cell, data_cell});
    ton::SmartContract::Args args;
    args.set_now(td::Time::now());
    args.set_address(std::move(address));

    args.set_method_id("get_wallet_data");
    auto res = smc.run_get_method(args);

    const int return_stack_size = 4;
    const vm::StackEntry::Type return_types[return_stack_size] = {vm::StackEntry::Type::t_int, vm::StackEntry::Type::t_slice, vm::StackEntry::Type::t_slice, vm::StackEntry::Type::t_cell};

    if (!res.success || res.stack->depth() != return_stack_size) {
      promise.set_error(td::Status::Error(204, "Jetton Wallet not found"));
      return;
    }

    auto stack = res.stack->as_span();
    
    for (int i = 0; i < 4; i++) {
      if (stack[i].type() != return_types[i]) {
        promise.set_error(td::Status::Error(204, "Jetton Wallet not found"));
        return;
      }
    }

    JettonWalletData data;
    data.address = convert::to_raw_address(address);
    data.balance = stack[0].as_int()->to_long();
    auto owner = convert::to_raw_address(stack[1].as_slice());
    if (owner.is_error()) {
      promise.set_error(owner.move_as_error());
      return;
    }
    data.owner = owner.move_as_ok();
    auto jetton = convert::to_raw_address(stack[2].as_slice());
    if (jetton.is_error()) {
      promise.set_error(jetton.move_as_error());
      return;
    }
    data.jetton = jetton.move_as_ok();
    data.last_transaction_lt = last_tx_lt;
    data.code_hash = code_cell->get_hash();
    data.data_hash = data_cell->get_hash();

    if (stack[3].as_cell()->get_hash() != code_cell->get_hash()) {
      LOG(WARNING) << "Jetton Wallet code hash mismatch: " << stack[3].as_cell()->get_hash().to_hex() << " != " << code_cell->get_hash().to_hex();
    }

    verify_belonging_to_master(std::move(data), std::move(promise));
  }

private:
  // check belonging of address to Jetton Master by calling get_wallet_address
  void verify_belonging_to_master(JettonWalletData data, td::Promise<JettonWalletData> &&promise) {
    auto P = td::PromiseCreator::lambda([SelfId = actor_id(this), this, data, promise = std::move(promise)](td::Result<block::StdAddress> R) mutable {
      if (R.is_error()) {
        LOG(ERROR) << "Failed to get wallet address from master: " << R.error();
        promise.set_error(R.move_as_error());
      } else {
        auto address = R.move_as_ok();
        if (convert::to_raw_address(address) != data.address) {
          LOG(ERROR) << "Jetton Master returned wrong address: " << convert::to_raw_address(address);
          promise.set_error(td::Status::Error(404, "Couldn't verify Jetton Wallet. Possibly scam."));
        } else {
          this->add_to_cache(address, data.code_hash, data);
          promise.set_value(std::move(data));
        }
      }
    });

    //TODO: handle address parsing errors
    block::StdAddress master_addr = block::StdAddress::parse(data.jetton).move_as_ok();
    block::StdAddress owner_addr = block::StdAddress::parse(data.owner).move_as_ok();
    td::actor::send_closure(jetton_master_detector_, &JettonMasterDetector::get_wallet_address, master_addr, owner_addr, std::move(P));
  }


  td::Result<JettonWalletData> check_db(block::StdAddress address) {
    return td::Status::Error("Not implemented");
  }

  td::Result<JettonWalletData> check_cache(block::StdAddress address) {
    auto it = cache_.find(convert::to_raw_address(address));
    if (it != cache_.end()) {
      return it->second;
    }
    auto db_res = check_db(address);
    if (db_res.is_ok()) {
      cache_.emplace(convert::to_raw_address(address), db_res.move_as_ok());
      return db_res.move_as_ok();
    }
    return td::Status::Error(404, "Jetton Master not found in cache/db");
  }

  td::Status add_to_db(block::StdAddress address, vm::CellHash code_hash, JettonWalletData data) {
    return td::Status::Error("Not implemented");
  }

  td::Status add_to_cache(block::StdAddress address, vm::CellHash code_hash, JettonWalletData data) {
    cache_.emplace(convert::to_raw_address(address), data);
    
    add_to_db(address, code_hash, data); // TODO: log if error

    return td::Status::OK();
  }
};