#include <block/block.h>
#include <td/actor/actor.h>
#include "td/actor/MultiPromise.h"
#include <mc-config.h>
#include "Jettons.h"
#include "convert-utils.h"

using SmcInterfaceR = std::variant<JettonMasterDetectorR::Result,
                                  JettonWalletDetectorR::Result,
                                  NftItemDetectorR::Result,
                                  NftCollectionDetectorR::Result>;

class InterfacesDetector: public td::actor::Actor {
public:
  InterfacesDetector(block::StdAddress address, 
                    td::Ref<vm::Cell> code_cell,
                    td::Ref<vm::Cell> data_cell, 
                    AllShardStates shard_states,
                    std::shared_ptr<block::ConfigInfo> config,
                    td::Promise<std::vector<SmcInterfaceR>> promise);

  void start_up() override;
private:
  block::StdAddress address_;
  td::Ref<vm::Cell> code_cell_;
  td::Ref<vm::Cell> data_cell_;
  AllShardStates shard_states_;
  std::shared_ptr<block::ConfigInfo> config_;

  std::shared_ptr<std::vector<SmcInterfaceR>> found_interfaces_;
  td::Promise<std::vector<SmcInterfaceR>> promise_;

  template<typename Detector>
  void detect_interface(td::Promise<td::Unit> promise);
};