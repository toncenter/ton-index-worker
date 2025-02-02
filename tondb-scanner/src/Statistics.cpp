#include "Statistics.h"

HistogramBucketMapper::HistogramBucketMapper() {
  // If you change this, you also need to change
  // size of array buckets_ in HistogramImpl
  bucketValues_ = {1, 2};
  double bucket_val = static_cast<double>(bucketValues_.back());
  while ((bucket_val = 1.5 * bucket_val) <=
         static_cast<double>(std::numeric_limits<uint64_t>::max())) {
    bucketValues_.push_back(static_cast<uint64_t>(bucket_val));
    // Extracts two most significant digits to make histogram buckets more
    // human-readable. E.g., 172 becomes 170.
    uint64_t pow_of_ten = 1;
    while (bucketValues_.back() / 10 > 10) {
      bucketValues_.back() /= 10;
      pow_of_ten *= 10;
    }
    bucketValues_.back() *= pow_of_ten;
  }
  maxBucketValue_ = bucketValues_.back();
  minBucketValue_ = bucketValues_.front();
}

size_t HistogramBucketMapper::IndexForValue(const uint64_t value) const {
  auto beg = bucketValues_.begin();
  auto end = bucketValues_.end();
  if (value >= maxBucketValue_)
    return end - beg - 1;  // bucketValues_.size() - 1
  else
    return std::lower_bound(beg, end, value) - beg;
}

namespace {
const HistogramBucketMapper bucketMapper;
}

void CountMetric::add(uint64_t delta) {
    count_.fetch_add(delta, std::memory_order_relaxed);
}

uint64_t CountMetric::get() const {
    return count_.load(std::memory_order_relaxed);
}

TimingMetric::TimingMetric(): num_buckets_(bucketMapper.BucketCount()) {
}

void TimingMetric::add(uint64_t duration) {
    count_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(duration, std::memory_order_relaxed);
    update_max(max_, duration);

    size_t index = bucketMapper.IndexForValue(duration);
    buckets_[index].fetch_add(1, std::memory_order_relaxed);
}

uint64_t TimingMetric::get_count() const {
    return count_.load(std::memory_order_relaxed);
}

uint64_t TimingMetric::get_sum() const {
    return sum_.load(std::memory_order_relaxed);
}

double TimingMetric::compute_percentile(double percentile) const {
    uint64_t total = get_count();
    if (total == 0) return 0.0;

    double threshold = total * (percentile / 100.0);
    uint64_t accumulated = 0;
    size_t bucket_idx = 0;

    for (; bucket_idx < num_buckets_; ++bucket_idx) {
        uint64_t bucket_count = buckets_[bucket_idx].load(std::memory_order_relaxed);
        if (accumulated + bucket_count >= threshold) break;
        accumulated += bucket_count;
    }

    uint64_t lower = 0, upper = 0;
    if (bucket_idx == 0) {
        upper = bucketMapper.FirstValue();
    } else if (bucket_idx < num_buckets_) {
        lower = bucketMapper.BucketLimit(bucket_idx - 1) + 1;
        upper = bucketMapper.BucketLimit(bucket_idx);
    } else {
        lower = bucketMapper.LastValue() + 1;
        upper = max_.load(std::memory_order_relaxed);
    }

    uint64_t bucket_count = buckets_[bucket_idx].load(std::memory_order_relaxed);
    if (bucket_count == 0) return upper;

    double fraction = (threshold - accumulated) / bucket_count;
    return lower + fraction * (upper - lower);
}

uint64_t TimingMetric::get_max() const {
    return max_.load(std::memory_order_relaxed);
}

size_t TimingMetric::find_bucket(uint64_t value) const {
    for (size_t i = 0; i < num_buckets_; ++i) {
        if (value <= bucketMapper.BucketLimit(i)) return i;
    }
    return num_buckets_ - 1;
}

void TimingMetric::update_max(std::atomic<uint64_t>& max, uint64_t value) {
    uint64_t current = max.load(std::memory_order_relaxed);
    while (value > current && !max.compare_exchange_weak(current, value, std::memory_order_relaxed));
}

void Statistics::record_time(const std::string& name, uint64_t duration, uint32_t count) {
    if (count > 1) {
        // for multiple events, we average the duration
        duration /= count;
    }
    std::shared_lock lock(timings_mutex_);
    auto it = timing_metrics_.find(name);
    if (it != timing_metrics_.end()) {
        it->second->add(duration);
        return;
    }
    lock.unlock();

    std::unique_lock ulock(timings_mutex_);
    it = timing_metrics_.find(name);
    if (it != timing_metrics_.end()) {
        it->second->add(duration);
        return;
    }

    auto metric = std::make_unique<TimingMetric>();
    metric->add(duration);
    timing_metrics_[name] = std::move(metric);
}

void Statistics::record_count(const std::string& name, uint64_t delta) {
    std::shared_lock lock(counts_mutex_);
    auto it = count_metrics_.find(name);
    if (it != count_metrics_.end()) {
        it->second->add(delta);
        return;
    }
    lock.unlock();

    std::unique_lock ulock(counts_mutex_);
    it = count_metrics_.find(name);
    if (it != count_metrics_.end()) {
        it->second->add(delta);
        return;
    }

    auto metric = std::make_unique<CountMetric>();
    metric->add(delta);
    count_metrics_[name] = std::move(metric);
}

std::string Statistics::generate_report_and_reset() const {
    std::unique_lock timings_lock(timings_mutex_);
    auto&& timing_metrics = std::move(timing_metrics_);
    timings_lock.unlock();
    std::unique_lock counts_lock(counts_mutex_);
    auto&& count_metrics = std::move(count_metrics_);
    counts_lock.unlock();

    std::ostringstream oss;

    for (const auto& [name, metric] : timing_metrics) {
        uint64_t count = metric->get_count();
        uint64_t sum = metric->get_sum();
        double p50 = metric->compute_percentile(50.0);
        double p95 = metric->compute_percentile(95.0);
        double p99 = metric->compute_percentile(99.0);
        uint64_t p100 = metric->get_max();

        oss << name << " P50 : " << p50 << " P95 : " << p95 << " P99 : " << p99 << " P100 : " << p100
            << " COUNT : " << count << " SUM : " << sum << std::endl;
    }

    for (const auto& [name, metric] : count_metrics) {
        oss << name << " COUNT : " << metric->get() << std::endl;
    }

    return oss.str();
}

Statistics g_statistics;