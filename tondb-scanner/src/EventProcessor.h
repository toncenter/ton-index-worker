#pragma once
#include "InterfaceDetectors.hpp"


// Detects special cases of Actions like - Jetton transfers and burns, NFT transfers
class ActionDetector: public td::actor::Actor {
private:
  ParsedBlockPtr block_;
  td::Promise<ParsedBlockPtr> promise_;
public:
  ActionDetector(ParsedBlockPtr block, td::Promise<ParsedBlockPtr> promise): block_(block), promise_(std::move(promise)) {
  }

  void start_up() override {
    for (const auto& block : block_->blocks_) {
      for (const auto& transaction : block.transactions) {
        process_tx(transaction);
      }
    }
    promise_.set_value(std::move(block_));
    stop();
  }

  void process_tx(const schema::Transaction& transaction) {
    auto interfaces_it = block_->account_interfaces_.find(transaction.account);

    if (interfaces_it == block_->account_interfaces_.end()) {
      return;
    }
    auto interfaces = interfaces_it->second;

    if (!transaction.in_msg) {
      return;
    }
    
    auto in_msg_body_cs = vm::load_cell_slice_ref(transaction.in_msg.value().body);

    for (auto& v : interfaces) {
      if (auto jetton_wallet_ptr = std::get_if<JettonWalletDataV2>(&v)) {
        if (tokens::gen::t_InternalMsgBody.check_tag(*in_msg_body_cs) == tokens::gen::InternalMsgBody::transfer_jetton) {
          auto transfer = parse_jetton_transfer(*jetton_wallet_ptr, transaction, in_msg_body_cs);
          if (transfer.is_error()) {
            LOG(DEBUG) << "Failed to parse jetton transfer: " << transfer.move_as_error();
          } else {
            block_->events_.push_back(transfer.move_as_ok());
          }
        } else if (tokens::gen::t_InternalMsgBody.check_tag(*in_msg_body_cs) == tokens::gen::InternalMsgBody::burn) {
          auto burn = parse_jetton_burn(*jetton_wallet_ptr, transaction, in_msg_body_cs);
          if (burn.is_error()) {
            LOG(DEBUG) << "Failed to parse jetton burn: " << burn.move_as_error();
          } else {
            block_->events_.push_back(burn.move_as_ok());
          }
        } else if (tokens::gen::t_InternalMsgBody.check_tag(*in_msg_body_cs) == tokens::gen::InternalMsgBody::internal_transfer) {
            auto mint = parse_jetton_mint(*jetton_wallet_ptr, transaction, in_msg_body_cs);
            if (mint.is_error()) {
                std::cout << "Failed to parse_jetton_mint" <<  mint.move_as_error().message().str() << std::endl;
                LOG(DEBUG) << "Failed to parse jetton mint: " << mint.move_as_error();
            } else {
                block_->events_.push_back(mint.move_as_ok());
            }
        }
      }

      if (auto nft_item_ptr = std::get_if<NFTItemDataV2>(&v)) {
        if (tokens::gen::t_InternalMsgBody.check_tag(*in_msg_body_cs) == tokens::gen::InternalMsgBody::transfer_nft) {
          auto transfer = parse_nft_transfer(*nft_item_ptr, transaction, in_msg_body_cs);
          if (transfer.is_error()) {
            LOG(DEBUG) << "Failed to parse nft transfer: " << transfer.move_as_error();
          } else {
            block_->events_.push_back(transfer.move_as_ok());
          }
        }
      }
    }
  }

  td::Result<JettonTransfer> parse_jetton_transfer(const JettonWalletDataV2& jetton_wallet, const schema::Transaction& transaction, td::Ref<vm::CellSlice> in_msg_body_cs) {
    tokens::gen::InternalMsgBody::Record_transfer_jetton transfer_record;
    if (!tlb::csr_unpack_inexact(in_msg_body_cs, transfer_record)) {
      return td::Status::Error("Failed to unpack transfer");
    }

    JettonTransfer transfer;
    transfer.trace_id = transaction.trace_id;
    transfer.transaction_hash = transaction.hash;
    transfer.transaction_lt = transaction.lt;
    transfer.transaction_now = transaction.now;
    if (auto* v = std::get_if<schema::TransactionDescr_ord>(&transaction.description)) {
      transfer.transaction_aborted = v->aborted;
    } else {
      return td::Status::Error("Unexpected transaction description");
    }

    transfer.query_id = transfer_record.query_id;
    transfer.amount = block::tlb::t_VarUInteger_16.as_integer(transfer_record.amount);
    if (transfer.amount.is_null()) {
      return td::Status::Error("Failed to unpack transfer amount");
    }
    if (!transaction.in_msg || !transaction.in_msg->source) {
      return td::Status::Error("Failed to unpack transfer source");
    }
    transfer.source = transaction.in_msg->source.value();
    transfer.jetton_wallet = convert::to_raw_address(transaction.account);
    transfer.jetton_master = convert::to_raw_address(jetton_wallet.jetton);
    auto destination = convert::to_raw_address(transfer_record.destination);
    if (destination.is_error()) {
      return destination.move_as_error_prefix("Failed to unpack transfer destination: ");
    }
    transfer.destination = destination.move_as_ok();
    auto response_destination = convert::to_raw_address(transfer_record.response_destination);
    if (response_destination.is_error()) {
      return response_destination.move_as_error_prefix("Failed to unpack transfer response destination: ");
    }
    transfer.response_destination = response_destination.move_as_ok();
    if (!transfer_record.custom_payload.write().fetch_maybe_ref(transfer.custom_payload)) {
      return td::Status::Error("Failed to fetch custom payload");
    }
    transfer.forward_ton_amount = block::tlb::t_VarUInteger_16.as_integer(transfer_record.forward_ton_amount);
    if (!transfer_record.forward_payload.write().fetch_maybe_ref(transfer.forward_payload)) {
      return td::Status::Error("Failed to fetch forward payload");
    }

    return transfer;
  }

  td::Result<JettonBurn> parse_jetton_burn(const JettonWalletDataV2& jetton_wallet, const schema::Transaction& transaction, td::Ref<vm::CellSlice> in_msg_body_cs) {
    tokens::gen::InternalMsgBody::Record_burn burn_record;
    if (!tlb::csr_unpack_inexact(in_msg_body_cs, burn_record)) {
      return td::Status::Error("Failed to unpack burn");
    }

    JettonBurn burn;
    burn.trace_id = transaction.trace_id;
    burn.transaction_hash = transaction.hash;
    burn.transaction_lt = transaction.lt;
    burn.transaction_now = transaction.now;
    if (auto* v = std::get_if<schema::TransactionDescr_ord>(&transaction.description)) {
      burn.transaction_aborted = v->aborted;
    } else {
      return td::Status::Error("Unexpected transaction description");
    }

    burn.query_id = burn_record.query_id;
    if (!transaction.in_msg || !transaction.in_msg->source) {
      return td::Status::Error("Failed to unpack burn source");
    }
    burn.owner = transaction.in_msg->source.value();
    burn.jetton_wallet = convert::to_raw_address(transaction.account);
    burn.jetton_master = convert::to_raw_address(jetton_wallet.jetton);
    burn.amount = block::tlb::t_VarUInteger_16.as_integer(burn_record.amount);
    if (burn.amount.is_null()) {
      return td::Status::Error("Failed to unpack burn amount");
    }
    auto response_destination = convert::to_raw_address(burn_record.response_destination);
    if (response_destination.is_error()) {
      return response_destination.move_as_error_prefix("Failed to unpack burn response destination: ");
    }
    burn.response_destination = response_destination.move_as_ok();
    if (!burn_record.custom_payload.write().fetch_maybe_ref(burn.custom_payload)) {
      return td::Status::Error("Failed to fetch custom payload");
    }

    return burn;
  }


    td::Result<JettonMint> parse_jetton_mint(const JettonWalletDataV2& jetton_wallet, const schema::Transaction& transaction, td::Ref<vm::CellSlice> in_msg_body_cs) {
        std::cout << "Enter to parse_jetton_mint" << std::endl;
        tokens::gen::InternalMsgBody::Record_internal_transfer internal_transfer_record;
        if (!tlb::csr_unpack_inexact(in_msg_body_cs, internal_transfer_record)) {
            return td::Status::Error("Failed to unpack internal_transfer");
        }

        JettonMint mint;
        mint.trace_id = transaction.trace_id;
        mint.transaction_hash = transaction.hash;
        mint.transaction_lt = transaction.lt;
        mint.transaction_now = transaction.now;
        if (auto* v = std::get_if<schema::TransactionDescr_ord>(&transaction.description)) {
            mint.transaction_aborted = v->aborted;
        } else {
            return td::Status::Error("Unexpected transaction description");
        }

        mint.query_id = internal_transfer_record.query_id;
        if (!transaction.in_msg || !transaction.in_msg->source) {
            return td::Status::Error("Failed to unpack burn source");
        }

        mint.amount = block::tlb::t_VarUInteger_16.as_integer(internal_transfer_record.amount);
        if (mint.amount.is_null()) {
            return td::Status::Error("Failed to unpack burn amount");
        }

        // never delete this!
        auto initiator_address = transaction.in_msg->source.value();
        auto master_address = convert::to_raw_address(jetton_wallet.jetton);
        if(initiator_address != master_address) {
            return td::Status::Error("internal_transfer is not mint message");
        }

        mint.owner = convert::to_raw_address(jetton_wallet.owner);
        mint.jetton_wallet = convert::to_raw_address(transaction.account);
        mint.jetton_master = convert::to_raw_address(jetton_wallet.jetton);

        auto response_destination = convert::to_raw_address(internal_transfer_record.response_address);
        if (response_destination.is_error()) {
            return response_destination.move_as_error_prefix("Failed to unpack mint response address: ");
        }
        mint.response_destination = response_destination.move_as_ok();

        mint.forward_ton_amount = block::tlb::t_VarUInteger_16.as_integer(internal_transfer_record.forward_ton_amount);
        if (!internal_transfer_record.forward_payload.write().fetch_maybe_ref(mint.forward_payload)) {
            return td::Status::Error("Failed to fetch forward payload");
        }
        return mint;
    }

  td::Result<NFTTransfer> parse_nft_transfer(const NFTItemDataV2& nft_item, const schema::Transaction& transaction, td::Ref<vm::CellSlice> in_msg_body_cs) {
    tokens::gen::InternalMsgBody::Record_transfer_nft transfer_record;
    if (!tlb::csr_unpack_inexact(in_msg_body_cs, transfer_record)) {
      return td::Status::Error("Failed to unpack transfer");
    }

    NFTTransfer transfer;
    transfer.trace_id = transaction.trace_id;
    transfer.transaction_hash = transaction.hash;
    transfer.transaction_lt = transaction.lt;
    transfer.transaction_now = transaction.now;
    if (auto* v = std::get_if<schema::TransactionDescr_ord>(&transaction.description)) {
      transfer.transaction_aborted = v->aborted;
    } else {
      transfer.transaction_aborted = 0;
    }

    transfer.query_id = transfer_record.query_id;
    transfer.nft_item = transaction.account;
    transfer.nft_item_index = nft_item.index;
    if (nft_item.collection_address.has_value()) {
      transfer.nft_collection = convert::to_raw_address(nft_item.collection_address.value());
    }
    if (!transaction.in_msg.has_value() || !transaction.in_msg.value().source) {
      return td::Status::Error("Failed to fetch NFT old owner address");
    }
    transfer.old_owner = transaction.in_msg.value().source.value();
    auto new_owner = convert::to_raw_address(transfer_record.new_owner);
    if (new_owner.is_error()) {
      return new_owner.move_as_error_prefix("Failed to unpack new owner address: ");
    }
    transfer.new_owner = new_owner.move_as_ok();
    auto response_destination = convert::to_raw_address(transfer_record.response_destination);
    if (response_destination.is_error()) {
      return response_destination.move_as_error_prefix("Failed to unpack response destination: ");
    }
    transfer.response_destination = response_destination.move_as_ok();
    if (!transfer_record.custom_payload.write().fetch_maybe_ref(transfer.custom_payload)) {
      return td::Status::Error("Failed to fetch custom payload");
    }
    transfer.forward_amount = block::tlb::t_VarUInteger_16.as_integer(transfer_record.forward_amount);
    if (!transfer_record.forward_payload.write().fetch_maybe_ref(transfer.forward_payload)) {
      return td::Status::Error("Failed to fetch forward payload");
    }

    return transfer;
  }
};

// todo implement mint event for postgres-v1 & clickhouse
class EventProcessor: public td::actor::Actor {
private:
  td::actor::ActorOwn<InterfaceManager> interface_manager_;
  td::actor::ActorOwn<JettonMasterDetector> jetton_master_detector_;
  td::actor::ActorOwn<JettonWalletDetector> jetton_wallet_detector_;
  td::actor::ActorOwn<NFTCollectionDetector> nft_collection_detector_;
  td::actor::ActorOwn<NFTItemDetector> nft_item_detector_;
public:
  EventProcessor(td::actor::ActorId<InsertManagerInterface> insert_manager): 
    interface_manager_(td::actor::create_actor<InterfaceManager>("interface_manager", insert_manager)),
    jetton_master_detector_(td::actor::create_actor<JettonMasterDetector>("jetton_master_detector", interface_manager_.get(), insert_manager)), 
    jetton_wallet_detector_(td::actor::create_actor<JettonWalletDetector>("jetton_wallet_detector", jetton_master_detector_.get(), interface_manager_.get(), insert_manager)),
    nft_collection_detector_(td::actor::create_actor<NFTCollectionDetector>("nft_collection_detector", interface_manager_.get(), insert_manager)),
    nft_item_detector_(td::actor::create_actor<NFTItemDetector>("nft_item_detector", interface_manager_.get(), insert_manager, nft_collection_detector_.get())) {
  }

  void process(ParsedBlockPtr block, td::Promise<> &&promise);
  // void process(ParsedBlockPtr block, td::Promise<ParsedBlockPtr> promise);

private:
  void process_states(const std::vector<schema::AccountState>& account_states, const MasterchainBlockDataState& blocks_ds, td::Promise<std::vector<BlockchainInterface>> &&promise);
  void process_transactions(const std::vector<schema::Transaction>& transactions, td::Promise<std::vector<BlockchainEvent>> &&promise);
};
