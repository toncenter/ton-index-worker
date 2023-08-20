#include "IndexScheduler.h"


void IndexScheduler::start_up() {
    LOG(INFO) << "IndexScheduler start up";
}

void IndexScheduler::alarm() {
    LOG(INFO) << "IndexScheduler alarm";
}

void IndexScheduler::run() {
    alarm_timestamp() = td::Timestamp.in(1.0);
}
