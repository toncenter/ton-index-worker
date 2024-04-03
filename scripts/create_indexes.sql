begin;

-- blocks
create index if not exists blocks_index_2 on blocks (gen_utime);
create index if not exists blocks_index_3 on blocks (mc_block_workchain, mc_block_shard, mc_block_seqno);
create index if not exists blocks_index_4 on blocks (seqno) where (workchain = '-1'::integer);
create index if not exists blocks_index_5 on blocks (start_lt);

-- transactions
create index if not exists transactions_index_1 on transactions (block_workchain, block_shard, block_seqno);
create index if not exists event_detector__transaction_index_1 on transactions (lt) where (trace_id IS NULL);
create index if not exists transactions_index_2 on transactions (account, lt);
create index if not exists transactions_index_2a on transactions (account, now);
create index if not exists transactions_index_3 on transactions (lt, hash);
create index if not exists transactions_index_4 on transactions (now, hash);
create index if not exists transactions_index_5 on transactions (hash);
create index if not exists transactions_index_6 on transactions (trace_id);
create index if not exists transactions_index_8 on transactions (mc_block_seqno);





commit;
