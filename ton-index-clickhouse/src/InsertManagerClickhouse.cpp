#include "td/utils/StringBuilder.h"
#include "InsertManagerClickhouse.h"
#include "clickhouse/client.h"


void InsertManagerClickhouse::start_up() {
    LOG(INFO) << "Clickhouse start_up";
    try {
        {
            clickhouse::ClientOptions default_options;
            default_options.SetHost(credential_.host);
            default_options.SetPort(credential_.port);
            default_options.SetUser(credential_.user);
            default_options.SetPassword(credential_.password);
            default_options.SetDefaultDatabase("default");

            clickhouse::Client client(default_options);
            td::StringBuilder builder;
            builder << "CREATE DATABASE IF NOT EXISTS " << credential_.dbname << ";";
            client.Execute(builder.as_cslice().str());
            LOG(INFO) << "Database " << credential_.dbname << " created";
        }
        auto options = credential_.get_clickhouse_options();
        clickhouse::Client client(options);
        {
            td::StringBuilder builder;
            builder << "CREATE TABLE IF NOT EXISTS blocks ("
                    << "workchain Int32, "
                    << "shard Int64, "
                    << "seqno Int32, "
                    << "root_hash FixedString(44), "
                    << "file_hash FixedString(44), "
                    << "mc_block_seqno Int32, "
                    << "global_id Int32, "
                    << "version Int32, "
                    << "after_merge Bool, "
                    << "before_split Bool, "
                    << "after_split Bool, "
                    << "want_merge Bool, "
                    << "want_split Bool, "
                    << "key_block Bool, "
                    << "vert_seqno_incr Bool, "
                    << "flags Int32, "
                    << "gen_utime Int32, "
                    << "start_lt UInt64, "
                    << "end_lt UInt64, "
                    << "validator_list_hash_short Int32, "
                    << "gen_catchain_seqno Int32, "
                    << "min_ref_mc_seqno Int32, "
                    << "prev_key_block_seqno Int32, "
                    << "vert_seqno Int32, "
                    << "master_ref_seqno Nullable(Int32), "
                    << "rand_seed FixedString(44), "
                    << "created_by FixedString(44), "
                    << "prev_blocks Array(Tuple(Int32, Int64, Int32)), "
                    << "shards Array(Tuple(Int32, Int64, Int32)), "
                    << "transaction_count Int32"
                    << ") ENGINE ReplacingMergeTree PRIMARY KEY(workchain, shard, seqno);\n";
            client.Execute(builder.as_cslice().str());
            LOG(INFO) << "Table blocks created";
        }
        {
            td::StringBuilder builder;    
            builder << "CREATE TABLE IF NOT EXISTS transactions ("
                    << "hash FixedString(44), "
                    << "lt UInt64, "
                    << "account FixedString(68), "
                    << "prev_hash FixedString(44), "
                    << "prev_lt UInt64, "
                    << "now UInt32, "
                    << "orig_status Enum('uninit' = 0, 'frozen' = 1, 'active' = 2, 'nonexist' = 3), "
                    << "end_status Enum('uninit' = 0, 'frozen' = 1, 'active' = 2, 'nonexist' = 3), "
                    << "total_fees UInt64, "
                    << "account_state_hash_before FixedString(44), "
                    << "account_state_hash_after FixedString(44), "
                    << "block_workchain Int32, "
                    << "block_shard Int64, "
                    << "block_seqno Int32, "
                    << "mc_block_seqno Int32, "
                    // flattened description
                    << "descr Enum('ord' = 0, 'storage' = 1, 'tick_tock' = 2, 'split_prepare' = 4, 'split_install' = 5, 'merge_prepare' = 6, 'merge_install' = 7), "

                    << "aborted Bool, "
                    << "destroyed Bool, "
                    << "ord__credit_first Bool, "
                    << "tick_tock__is_tick_tock Bool, "
                    << "split_install__installed Bool, "

                    << "storage_ph__storage_fees_collected Nullable(UInt64), "
                    << "storage_ph__storage_fees_due Nullable(UInt64), "
                    << "storage_ph__status_change Nullable( Enum('acst_unchanged' = 0, 'acst_frozen' = 2, 'acst_deleted' = 3) ), "

                    << "credit_ph__due_fees_collected Nullable(UInt64), "
                    << "credit_ph__credit Nullable(UInt64), "

                    << "compute_ph Nullable(Enum('skipped' = 0, 'vm' = 1)), "
                    << "compute_ph__skipped__reason Nullable(Enum('cskip_no_state' = 0, 'cskip_bad_state' = 1, 'cskip_no_gas' = 2, 'cskip_suspended' = 5)), "
                    << "compute_ph__vm__success Nullable(Bool), "
                    << "compute_ph__vm__msg_state_used Nullable(Bool), "
                    << "compute_ph__vm__account_activated Nullable(Bool), "
                    << "compute_ph__vm__gas_fees Nullable(UInt64), "
                    << "compute_ph__vm__gas_used Nullable(UInt64), "
                    << "compute_ph__vm__gas_limit Nullable(UInt64), "
                    << "compute_ph__vm__gas_credit Nullable(UInt64), "
                    << "compute_ph__vm__mode Nullable(Int8), "
                    << "compute_ph__vm__exit_code Nullable(Int32), "
                    << "compute_ph__vm__exit_arg Nullable(Int32), "
                    << "compute_ph__vm__vm_steps Nullable(UInt32), "
                    << "compute_ph__vm__vm_init_state_hash Nullable(FixedString(44)), "
                    << "compute_ph__vm__vm_final_state_hash Nullable(FixedString(44)), "

                    << "action__success Nullable(Bool), "
                    << "action__valid Nullable(Bool), "
                    << "action__no_funds Nullable(Bool), "
                    << "action__status_change Nullable(Nullable(Enum('acst_unchanged' = 0, 'acst_frozen' = 2, 'acst_deleted' = 3))), "
                    << "action__total_fwd_fees Nullable(UInt64), "
                    << "action__total_action_fees Nullable(UInt64), "
                    << "action__result_code Nullable(Int32), "
                    << "action__result_arg Nullable(Int32), "
                    << "action__tot_actions Nullable(UInt16), "
                    << "action__spec_actions Nullable(UInt16), "
                    << "action__skipped_actions Nullable(UInt16), "
                    << "action__msgs_created Nullable(UInt16), "
                    << "action__action_list_hash Nullable(FixedString(44)), "
                    << "action__tot_msg_size__cells Nullable(UInt64), "
                    << "action__tot_msg_size__bits Nullable(UInt64), "

                    << "bounce Nullable(Enum('negfunds' = 0, 'nofunds' = 1, 'ok' = 2)), "
                    << "bounce__msg_size__cells Nullable(UInt64), "
                    << "bounce__msg_size__bits Nullable(UInt64), "
                    << "bounce__no_funds__req_fwd_fees Nullable(UInt64), "
                    << "bounce__ok__msg_fees Nullable(UInt64), "
                    << "bounce__ok__fwd_fees Nullable(UInt64), "

                    << "split_info__cur_shard_pfx_len Nullable(UInt8), "
                    << "split_info__acc_split_depth Nullable(UInt8), "
                    << "split_info__this_addr Nullable(FixedString(68)), "
                    << "split_info__sibling_addr Nullable(FixedString(68))"

                    << ") ENGINE ReplacingMergeTree PRIMARY KEY(hash, lt);\n";
            client.Execute(builder.as_cslice().str());
            LOG(INFO) << "Table transactions created";
        }
        {
            td::StringBuilder builder;    
            builder << "CREATE TABLE IF NOT EXISTS messages ("
                    << "tx_hash FixedString(44), "
                    << "tx_lt UInt64, "
                    << "tx_account FixedString(68), "
                    << "tx_now UInt32, "
                    << "block_workchain Int32, "
                    << "block_shard Int64, "
                    << "block_seqno Int32, "
                    << "mc_block_seqno Int32, "
                    << "direction Enum('in' = 0, 'out' = 1), "
                    << "hash FixedString(68), "
                    << "source Nullable(FixedString(68)), "
                    << "destination Nullable(FixedString(68)), "
                    << "value Nullable(UInt64), "
                    << "fwd_fee Nullable(UInt64), "
                    << "ihr_fee Nullable(UInt64), "
                    << "created_lt Nullable(UInt64), "
                    << "created_at Nullable(UInt32), "
                    << "opcode Nullable(Int32), "
                    << "ihr_disabled Nullable(Bool), "
                    << "bounce Nullable(Bool), "
                    << "bounced Nullable(Bool), "
                    << "import_fee Nullable(UInt64), "
                    << "body_hash FixedString(44), "
                    << "init_state_hash Nullable(FixedString(44))"
                    << ") ENGINE ReplacingMergeTree PRIMARY KEY(hash, lt);\n";
            client.Execute(builder.as_cslice().str());
            LOG(INFO) << "Table messages created";
        }
        {
            td::StringBuilder builder;    
            builder << "CREATE TABLE IF NOT EXISTS message_contents ("
                    << "hash FixedString(44), "
                    << "data String"
                    << ") ENGINE ReplacingMergeTree PRIMARY KEY(hash);\n";
            client.Execute(builder.as_cslice().str());
            LOG(INFO) << "Table message_contents created";
        }
        {
            td::StringBuilder builder;
            builder << "CREATE TABLE IF NOT EXISTS account_states ("
                    << "hash FixedString(44), "
                    << "account FixedString(68), "
                    << "timestamp UInt32, "
                    << "balance UInt64, "
                    << "account_status Enum('uninit' = 0, 'frozen' = 1, 'active' = 2, 'nonexist' = 3),"
                    << "frozen_hash Nullable(FixedString(44)), "
                    << "code_hash Nullable(FixedString(44)), "
                    << "data_hash Nullable(FixedString(44)), "
                    << "last_trans_lt UInt64, "
                    << ") ENGINE ReplacingMergeTree PRIMARY KEY(hash);\n";
            client.Execute(builder.as_cslice().str());
            LOG(INFO) << "Table account_states created";
        }
        {
            td::StringBuilder builder;
            builder << "CREATE TABLE IF NOT EXISTS latest_account_states ("
                    << "account FixedString(68), "
                    << "hash FixedString(44), "
                    << "timestamp UInt32, "
                    << "balance UInt64, "
                    << "account_status Enum('uninit' = 0, 'frozen' = 1, 'active' = 2, 'nonexist' = 3),"
                    << "frozen_hash Nullable(FixedString(44)), "
                    << "code_hash Nullable(FixedString(44)), "
                    << "code_boc Nullable(String), "
                    << "data_hash Nullable(FixedString(44)), "
                    << "data_boc Nullable(String), "
                    << "last_trans_lt UInt64, "
                    << ") ENGINE ReplacingMergeTree PRIMARY KEY(account);\n";
            client.Execute(builder.as_cslice().str());
            LOG(INFO) << "Table account_states created";
        }
        LOG(INFO) << "Clickhouse start_up finished";
    } catch (const std::exception& e) {
        LOG(FATAL) << "Clickhouse start_up error: " << e.what();
        std::_Exit(2);
    }

    alarm_timestamp() = td::Timestamp::in(1.0);
}

void InsertManagerClickhouse::create_insert_actor(std::vector<InsertTaskStruct> insert_tasks, td::Promise<td::Unit> promise) {
    td::actor::create_actor<InsertBatchClickhouse>("insert_batch_clickhouse", credential_.get_clickhouse_options(), std::move(insert_tasks), std::move(promise)).release();
}

void InsertManagerClickhouse::get_existing_seqnos(td::Promise<std::vector<std::uint32_t>> promise)
{
    LOG(INFO) << "Clickhouse get_existing_seqnos";
    try {
        auto options = credential_.get_clickhouse_options();
        clickhouse::Client client(options);
        
        std::vector<std::uint32_t> result;
        client.Select("SELECT seqno from blocks WHERE workchain = -1", [&result](const clickhouse::Block& block) {
            for (size_t i = 0; i < block.GetRowCount(); ++i) {
                result.push_back(block[0]->As<clickhouse::ColumnInt32>()->At(i));
            }
        });
        promise.set_result(std::move(result));
    } catch (std::exception& e) {
       promise.set_error(td::Status::Error(DB_ERROR, PSLICE() << "Failed to fetch existing seqnos: " << e.what()));
    }
    return;
}

void InsertManagerClickhouse::upsert_jetton_wallet(JettonWalletData jetton_wallet, td::Promise<td::Unit> promise) {
    LOG(ERROR) << "Unreachable code";
    promise.set_result(td::Unit());
}

void InsertManagerClickhouse::upsert_jetton_master(JettonMasterData jetton_wallet, td::Promise<td::Unit> promise) {
    LOG(ERROR) << "Unreachable code";
    promise.set_result(td::Unit());
}

void InsertManagerClickhouse::upsert_nft_collection(NFTCollectionData nft_collection, td::Promise<td::Unit> promise) {
    LOG(ERROR) << "Unreachable code";
    promise.set_result(td::Unit());
}

void InsertManagerClickhouse::upsert_nft_item(NFTItemData nft_item, td::Promise<td::Unit> promise) {
    LOG(ERROR) << "Unreachable code";
    promise.set_result(td::Unit());
}

clickhouse::ClientOptions InsertManagerClickhouse::Credential::get_clickhouse_options()
{
    clickhouse::ClientOptions options;
    options.SetHost(host);
    options.SetPort(port);
    options.SetUser(user);
    options.SetPassword(password);
    options.SetDefaultDatabase(dbname);
    return std::move(options);
}

void InsertBatchClickhouse::start_up() {
    clickhouse::Client client(client_options_);
    promise_.set_result(td::Unit());  // DEBUG:

    try{
        // TODO: Insert more info
        // insert_blocks(client, insert_tasks_);

        // all done
        for(auto& task_ : insert_tasks_) {
            task_.promise_.set_result(td::Unit());
        }
        promise_.set_result(td::Unit());
    } catch (const std::exception &e) {
        // something failed
        for(auto& task_ : insert_tasks_) {
            task_.promise_.set_error(td::Status::Error(ErrorCode::DB_ERROR, PSLICE() << "Error inserting to PG: " << e.what()));
        }
        promise_.set_error(td::Status::Error(ErrorCode::DB_ERROR, PSLICE() << "Error inserting to PG: " << e.what()));
    }
    stop();
}

// void InsertBatchClickhouse::insert_blocks(clickhouse::Client &client, const ){
//     clickhouse::Block block;

//     // using ColumnHash = clickhouse::ColumnFixedString(44);
//     using ColumnOptionalInt32 = clickhouse::ColumnNullableT<clickhouse::ColumnInt32>;

//     auto workchain = std::make_shared<clickhouse::ColumnInt32>();
//     auto shard = std::make_shared<clickhouse::ColumnInt64>();
//     auto seqno = std::make_shared<clickhouse::ColumnInt32>();
//     auto root_hash = std::make_shared<clickhouse::ColumnFixedString>(44);
//     auto file_hash = std::make_shared<clickhouse::ColumnFixedString>(44);
//     auto mc_seqno = std::make_shared<ColumnOptionalInt32>();
//     auto gen_utime = std::make_shared<clickhouse::ColumnInt32>();
//     auto start_lt = std::make_shared<clickhouse::ColumnUInt64>();
//     auto end_lt = std::make_shared<clickhouse::ColumnUInt64>();
//     auto transaction_count = std::make_shared<clickhouse::ColumnInt32>();

//     for(const auto& task_ : insert_tasks_) {
//         for(const auto& blk_ :task_.parsed_block_->blocks_) {
//             workchain->Append(blk_.workchain);
//             shard->Append(blk_.shard);
//             seqno->Append(blk_.seqno);
//             root_hash->Append(blk_.root_hash);
//             file_hash->Append(blk_.file_hash);
//             if (blk_.mc_block_seqno) {
//                 mc_seqno->Append(blk_.mc_block_seqno.value());
//             } else {
//                 mc_seqno->Append(std::nullopt);
//             }
//             gen_utime->Append(blk_.gen_utime);
//             start_lt->Append(blk_.start_lt);
//             end_lt->Append(blk_.end_lt);
//             transaction_count->Append(blk_.transactions.size());
//         }
//     }
//     block.AppendColumn("workchain", workchain);
//     block.AppendColumn("shard", shard);
//     block.AppendColumn("seqno", seqno);
//     block.AppendColumn("root_hash", root_hash);
//     block.AppendColumn("file_hash", file_hash);
//     block.AppendColumn("mc_seqno", mc_seqno);
//     block.AppendColumn("gen_utime", gen_utime);
//     block.AppendColumn("start_lt", start_lt);
//     block.AppendColumn("end_lt", end_lt);
//     block.AppendColumn("transaction_count", transaction_count);

//     client.Insert("blocks", block);

//     for(auto& task_ : insert_tasks_) {
//         task_.promise_.set_result(td::Unit());
//     }
// }
