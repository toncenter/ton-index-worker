#pragma once
#include <string>
#include <queue>
#include <clickhouse/client.h>
#include "InsertManagerBase.h"


class InsertBatchClickhouse;

class InsertManagerClickhouse: public InsertManagerBase {
public:
  struct Credential {
    std::string host = "127.0.0.1";
    int port = 9000;
    std::string user = "default";
    std::string password = "";
    std::string dbname = "default";

    clickhouse::ClientOptions get_clickhouse_options();
  };
  struct Options {};
private:
  InsertManagerClickhouse::Credential credential_;
public:
  InsertManagerClickhouse(Credential credential) : credential_(credential) {}

  void start_up() override;

  void create_insert_actor(std::vector<InsertTaskStruct> insert_tasks, td::Promise<td::Unit> promise) override;
  void get_existing_seqnos(td::Promise<std::vector<std::uint32_t>> promise) override;

  void upsert_jetton_wallet(JettonWalletData jetton_wallet, td::Promise<td::Unit> promise) override;
  void upsert_jetton_master(JettonMasterData jetton_wallet, td::Promise<td::Unit> promise) override;
  void upsert_nft_collection(NFTCollectionData nft_collection, td::Promise<td::Unit> promise) override;
  void upsert_nft_item(NFTItemData nft_item, td::Promise<td::Unit> promise) override;
};


class InsertBatchClickhouse: public td::actor::Actor {
public:
  InsertBatchClickhouse(clickhouse::ClientOptions client_options, std::vector<InsertTaskStruct> insert_tasks, td::Promise<td::Unit> promise) 
    : client_options_(std::move(client_options)), insert_tasks_(std::move(insert_tasks)), promise_(std::move(promise)) {}

  void start_up() override;
private:
  InsertManagerClickhouse::Credential credential_;
  clickhouse::ClientOptions client_options_;
  std::vector<InsertTaskStruct> insert_tasks_;
  td::Promise<td::Unit> promise_;

  struct MsgBody {
    td::Bits256 hash;
    std::string body;
  };

  void insert_transactions(clickhouse::Client& client);
  void insert_messages(clickhouse::Client& client);
  void insert_account_states(clickhouse::Client& client);
  void insert_blocks(clickhouse::Client& client);
  // void insert_shard_state(clickhouse::Client& client, const std::vector<InsertTaskStruct>& insert_tasks_);
  // void insert_transactions(clickhouse::Client& client, const std::vector<InsertTaskStruct>& insert_tasks_);
  // void insert_messsages(clickhouse::Client& client, const std::vector<schema::Message> &messages, const std::vector<MsgBody>& msg_bodies, const std::vector<TxMsg> &tx_msgs);
  // void insert_messages_contents(clickhouse::Client& client, const std::vector<MsgBody>& msg_bodies);
  // void insert_messages_impl(clickhouse::Client& client, const std::vector<schema::Message>& messages);
  // void insert_messages_txs(clickhouse::Client& client, const std::vector<TxMsg>& messages);
  // void insert_account_states(clickhouse::Client& client, const std::vector<InsertTaskStruct>& insert_tasks_);
  // void insert_latest_account_states(clickhouse::Client& client, const std::vector<InsertTaskStruct>& insert_tasks_);
};
