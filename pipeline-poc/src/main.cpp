#include <iostream>

#include "errorcode.h"
#include "td/utils/port/signals.h"
#include "td/utils/OptionParser.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/check.h"

#include "crypto/vm/cp0.h"

#include "IndexOptions.h"
#include "Scheduler.h"


td::Result<indexer::IndexOptions> parse_index_options(int argc, char **argv) {
  indexer::IndexOptions options;

  td::OptionParser p;
  p.set_description("Sandbox");
  p.add_option('\0', "help", "prints_help", [&]() {
    char b[10240];
    td::StringBuilder sb(td::MutableSlice{b, 10000});
    sb << p;
    std::cout << sb.as_cslice().c_str();
    std::exit(2);
  });
  p.add_option('D', "db", "Path to TON DB folder", [&](td::Slice fname) {
    options.db_root_ = fname.str();
  });
  p.add_option('p', "pg", "PostgreSQL connection string", [&](td::Slice fname) {
    options.pg_dsn_ = fname.str();
  });

  p.add_checked_option('f', "from", "Masterchain seqno to start indexing from", [&](td::Slice fname) {
    int v;
    try {
      v = std::stoi(fname.str());
    } catch (...) {
      return td::Status::Error(ton::ErrorCode::error, "bad value for --from: not a number");
    }
    options.from_seqno_ = v;
    return td::Status::OK();
  });
  // scheduler settings
  p.add_checked_option('t', "threads", "Scheduler threads (default: 7)", [&](td::Slice fname) {
    int v;
    try {
      v = std::stoi(fname.str());
    } catch (...) {
      return td::Status::Error(ton::ErrorCode::error, "bad value for --threads: not a number");
    }
    options.threads_ = v;
    return td::Status::OK();
  });
  auto S = p.run(argc, argv);
  if (S.is_error()) {
    LOG(ERROR) << "failed to parse options: " << S.move_as_error();
    return td::Status::Error("failed to parse options");
  }

  return std::move(options);
}


int main(int argc, char *argv[]) {
  SET_VERBOSITY_LEVEL(verbosity_INFO);
  td::set_default_failure_signal_handler().ensure();

  CHECK(vm::init_op_cp0());
  td::actor::ActorOwn<indexer::Scheduler> scheduler_;

  // options
  auto options_result_ = parse_index_options(argc, argv);
  if (options_result_.is_error()) {
    LOG(ERROR) << options_result_.move_as_error();
    std::_Exit(2);
  }
  auto options_ = options_result_.move_as_ok();
  td::actor::Scheduler scheduler({options_.threads_});

  scheduler.run_in_context([&] { 
    scheduler_ = td::actor::create_actor<indexer::Scheduler>("scheduler", std::move(options_));
  });
  
  while(scheduler.run(1)) {
    // do something
    // LOG(INFO) << "Tick";
  }
  LOG(INFO) << "Done!";
  return 0;
}
