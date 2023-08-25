#pragma once
#include <queue>
#include "validator/manager-disk.h"
#include "validator/db/rootdb.hpp"

#include "IndexData.h"
#include "InsertManagerPostgres.h"
#include "DataParser.h"
#include "EventProcessor.h"

class DbCacheWrapper;

class DbScanner: public td::actor::Actor {
private:
  std::string db_root_;
  td::uint32 last_known_seqno_{0};
  td::int32 max_db_actors_{32};
  td::int32 max_db_cache_size_{256};
  std::atomic<td::uint32> active_fetches;
  
  td::actor::ActorOwn<ton::validator::ValidatorManagerInterface> validator_manager_;
  td::actor::ActorOwn<ton::validator::RootDb> db_;
  td::actor::ActorOwn<DbCacheWrapper> db_caching_;


  // std::queue<std::uint32_t> seqnos_to_process_;
  // std::set<std::uint32_t> seqnos_in_progress_;
  // std::set<std::uint32_t> existing_mc_seqnos_;

  // td::actor::ActorOwn<EventProcessor> event_processor_;
  // td::actor::ActorId<InsertManagerInterface> insert_manager_;
  // td::actor::ActorId<ParseManager> parse_manager_;
public:
  DbScanner(std::string db_root, td::uint32 last_known_seqno, td::int32 max_db_actors) 
    : db_root_(db_root), last_known_seqno_(last_known_seqno), max_db_actors_(max_db_actors) {}

  td::uint32 get_last_known_seqno() {
    return last_known_seqno_;
  }

  void start_up() override;
  void alarm() override;

  void fetch_seqno(std::uint32_t mc_seqno, td::Promise<MasterchainBlockDataState> promise);
  void get_last_mc_seqno(td::Promise<std::int32_t> promise);
private:
  void set_last_mc_seqno(int mc_seqno);
  void catch_up_with_primary();
  void update_last_mc_seqno();

  // void schedule_for_processing();
  // void seqno_fetched(int mc_seqno, td::Result<MasterchainBlockDataState> blocks_data_state);
  // void seqno_parsed(int mc_seqno, td::Result<ParsedBlockPtr> parsed_block);
  // void interfaces_processed(int mc_seqno, ParsedBlockPtr parsed_block, td::Result<td::Unit> result);
  // void got_existing_seqnos(td::Result<std::vector<std::uint32_t>> R);
  // void seqno_completed(int mc_seqno);
  // void reschedule_seqno(int mc_seqno);
};

struct BlockIdExtHasher {
    std::size_t operator()(const ton::BlockIdExt& k) const {
        return std::hash<std::string>()(k.to_str());
    }
};

class DbCacheWrapper: public td::actor::Actor {
private:
  td::actor::ActorId<ton::validator::RootDb> db_;
  std::list<ton::BlockIdExt> block_data_cache_order_;
  std::unordered_map<ton::BlockIdExt, td::Ref<ton::validator::BlockData>, BlockIdExtHasher> block_data_cache_;
  std::unordered_map<ton::BlockIdExt, std::vector<td::Promise<td::Ref<ton::validator::BlockData>>>, BlockIdExtHasher> block_data_pending_requests_;

  std::list<ton::BlockIdExt> block_state_cache_order_;
  std::unordered_map<ton::BlockIdExt, td::Ref<ton::validator::ShardState>, BlockIdExtHasher> block_state_cache_;
  std::unordered_map<ton::BlockIdExt, std::vector<td::Promise<td::Ref<ton::validator::ShardState>>>, BlockIdExtHasher> block_state_pending_requests_;

  size_t max_cache_size_;

public:
  DbCacheWrapper(td::actor::ActorId<ton::validator::RootDb> db, size_t max_cache_size)
    : db_(db), max_cache_size_(max_cache_size) {
  }
  
  void get_block_data(ton::validator::ConstBlockHandle handle, td::Promise<td::Ref<ton::validator::BlockData>> promise);
  void got_block_data(ton::validator::ConstBlockHandle handle, td::Result<td::Ref<ton::validator::BlockData>> res);
  void get_block_state(ton::validator::ConstBlockHandle handle, td::Promise<td::Ref<ton::validator::ShardState>> promise);
  void got_block_state(ton::validator::ConstBlockHandle handle, td::Result<td::Ref<ton::validator::ShardState>> res);
};