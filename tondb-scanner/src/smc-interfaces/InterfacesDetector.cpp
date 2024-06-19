#include "InterfacesDetector.h"

InterfacesDetector::InterfacesDetector(block::StdAddress address, 
                    td::Ref<vm::Cell> code_cell,
                    td::Ref<vm::Cell> data_cell, 
                    AllShardStates shard_states,
                    std::shared_ptr<block::ConfigInfo> config,
                    td::Promise<std::vector<SmcInterfaceR>> promise) :
      address_(std::move(address)), code_cell_(std::move(code_cell)), data_cell_(std::move(data_cell)), 
      shard_states_(std::move(shard_states)), config_(std::move(config)), promise_(std::move(promise)) {
  found_interfaces_ = std::make_shared<std::vector<SmcInterfaceR>>();
}

template<typename Detector>
void InterfacesDetector::detect_interface(td::Promise<td::Unit> promise) {
  auto P = td::PromiseCreator::lambda([&, SelfId = actor_id(this), promise = std::move(promise)](td::Result<typename Detector::Result> data) mutable {
    if (data.is_ok()) {
      LOG(DEBUG) << "Detected interface " << typeid(typename Detector::Result).name() << " for " << convert::to_raw_address(address_);
      send_lambda(SelfId, [this, data = data.move_as_ok(), promise = std::move(promise)]() mutable {
        found_interfaces_->push_back(data);
        promise.set_value(td::Unit());
      });
    } else {
      promise.set_value(td::Unit());
    }
  });
  td::actor::create_actor<Detector>(td::Slice(typeid(Detector).name()), address_, code_cell_, data_cell_, shard_states_, config_, std::move(P)).release();
}

void InterfacesDetector::start_up() {
  auto P = td::PromiseCreator::lambda([&, SelfId=actor_id(this), promise = std::move(promise_)](td::Result<td::Unit> res) mutable {
    if (res.is_error()) {
      promise.set_error(res.move_as_error_prefix("Failed to detect interfaces: "));
      return;
    }
    promise.set_value(std::move(*found_interfaces_));
  });

  td::MultiPromise mp;
  auto ig = mp.init_guard();
  ig.add_promise(std::move(P));

  detect_interface<JettonWalletDetectorR>(ig.get_promise());
  detect_interface<JettonMasterDetectorR>(ig.get_promise());
  detect_interface<NftItemDetectorR>(ig.get_promise());
  detect_interface<NftCollectionDetectorR>(ig.get_promise());
}
