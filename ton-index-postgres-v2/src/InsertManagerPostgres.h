#pragma once
#include <queue>
#include <pqxx/pqxx>
#include "InsertManagerInterface.h"


struct InsertTask {
  std::uint32_t seqno_;
  ParsedBlockPtr data_;
  td::Promise<td::Unit> promise_;
};

struct InsertBatchTask {
  std::vector<InsertTask> tasks_;
  std::string query;
}

class InsertBatchPostgres;

class InsertManagerPostgres: public InsertManagerInterface {
public:
  struct Options {
    std::string connection_string_;
    std::int32_t max_data_depth_{-1};
  }
  InsertManagerPostgres(Options options) : options_(options) {}

  void start_up() override;

  void create_insert_actor(std::vector<InsertTaskStruct> insert_tasks, td::Promise<td::Unit> promise) override;
  void get_existing_seqnos(td::Promise<std::vector<std::uint32_t>> promise, std::int32_t from_seqno = 0, std::int32_t to_seqno = 0) override;
  void get_trace_assembler_state(td::Promise<schema::TraceAssemblerState> promise) override;

private:
  Options options_;
};

class InsertBatchPostgres: public td::actor::Actor {

};



// class InsertBatchPostgres;

// class InsertManagerPostgres: public InsertManagerBase {
// public:
//   struct Credential {
//     std::string host = "127.0.0.1";
//     int port = 5432;
//     std::string user;
//     std::string password;
//     std::string dbname = "ton_index";

//     std::string get_connection_string(std::string dbname = "") const;
//   };
// private:
//   InsertManagerPostgres::Credential credential_;
//   std::int32_t max_data_depth_ = 12;
// public:
//   InsertManagerPostgres(InsertManagerPostgres::Credential credential) : credential_(credential) {}

//   void start_up() override;

//   void set_max_data_depth(std::int32_t value);

//   void create_insert_actor(std::vector<InsertTaskStruct> insert_tasks, td::Promise<td::Unit> promise) override;
//   void get_existing_seqnos(td::Promise<std::vector<std::uint32_t>> promise, std::int32_t from_seqno = 0, std::int32_t to_seqno = 0) override;
//   void get_trace_assembler_state(td::Promise<schema::TraceAssemblerState> promise) override;
// };


// class InsertBatchPostgres: public td::actor::Actor {
// public:
//   InsertBatchPostgres(InsertManagerPostgres::Credential credential, std::vector<InsertTaskStruct> insert_tasks, td::Promise<td::Unit> promise, std::int32_t max_data_depth = 12) :
//     credential_(std::move(credential)), insert_tasks_(std::move(insert_tasks)), promise_(std::move(promise)), max_data_depth_(max_data_depth) {}

//   void start_up() override;
// private:
//   InsertManagerPostgres::Credential credential_;
//   std::string connection_string_;
//   std::vector<InsertTaskStruct> insert_tasks_;
//   td::Promise<td::Unit> promise_;
//   std::int32_t max_data_depth_;

//   std::string stringify(schema::ComputeSkipReason compute_skip_reason);
//   std::string stringify(schema::AccStatusChange acc_status_change);
//   std::string stringify(schema::AccountStatus account_status);
//   std::string stringify(schema::Trace::State state);

//   std::string insert_blocks(pqxx::work &txn);
//   std::string insert_shard_state(pqxx::work &txn);
//   std::string insert_transactions(pqxx::work &txn);
//   std::string insert_messages(pqxx::work &txn);
//   std::string insert_account_states(pqxx::work &txn);
//   std::string insert_latest_account_states(pqxx::work &txn);
//   std::string insert_jetton_transfers(pqxx::work &txn);
//   std::string insert_jetton_burns(pqxx::work &txn);
//   std::string insert_nft_transfers(pqxx::work &txn);
//   std::string insert_jetton_masters(pqxx::work &txn);
//   std::string insert_jetton_wallets(pqxx::work &txn);
//   std::string insert_nft_collections(pqxx::work &txn);
//   std::string insert_nft_items(pqxx::work &txn);
//   std::string insert_traces(pqxx::work &txn);
// };
