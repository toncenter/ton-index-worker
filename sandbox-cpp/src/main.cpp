#include <iostream>

#include "errorcode.h"
#include "td/utils/port/signals.h"
#include "td/utils/OptionParser.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/check.h"

#include "crypto/vm/cp0.h"
#include "td/actor/actor.h"

#include "Sandbox.h"
#include "SandboxActor.h"

int main(int argc, char *argv[]) {
    SET_VERBOSITY_LEVEL(verbosity_INFO);
    td::set_default_failure_signal_handler().ensure();

    CHECK(vm::init_op_cp0());

    // params
    std::string db_root = "/var/ton-work/db";
    std::int32_t threads = 7;

    td::OptionParser p;
    p.set_description("Sandbox");
    p.add_option('\0', "help", "prints_help", [&]() {
        char b[10240];
        td::StringBuilder sb(td::MutableSlice{b, 10000});
        sb << p;
        std::cout << sb.as_cslice().c_str();
        std::exit(2);
    });
    p.add_option('\0', "db", "Path to TON DB folder", [&](td::Slice value) {
        db_root = value.str();
    });
    auto S = p.run(argc, argv);
    if (S.is_error()) {
        LOG(ERROR) << "failed to parse arguments: " << S.move_as_error();
        std::_Exit(2);
    }

    // scheduler
    td::actor::ActorOwn<SandboxActor> sandbox;
    td::actor::Scheduler scheduler({threads});

    scheduler.run_in_context([&] {
        sandbox = td::actor::create_actor<SandboxActor>("sandbox", db_root);
    });
    while(scheduler.run(5)) {
        // do something
    }
    LOG(INFO) << "Finished!";
    return 0;
}
