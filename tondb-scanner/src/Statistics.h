#include <atomic>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <shared_mutex>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include "td/utils/ThreadLocalStorage.h"

class HistogramBucketMapper {
 public:
  HistogramBucketMapper();

  // converts a value to the bucket index.
  size_t IndexForValue(uint64_t value) const;
  // number of buckets required.

  size_t BucketCount() const { return bucketValues_.size(); }
  uint64_t LastValue() const { return maxBucketValue_; }
  uint64_t FirstValue() const { return minBucketValue_; }

  uint64_t BucketLimit(const size_t bucketNumber) const {
    // assert(bucketNumber < BucketCount());
    return bucketValues_[bucketNumber];
  }

 private:
  std::vector<uint64_t> bucketValues_;
  uint64_t maxBucketValue_;
  uint64_t minBucketValue_;
};

class CountMetric {
public:
    void add(uint64_t delta);
    uint64_t get() const;

private:
    std::atomic<uint64_t> count_{0};
};

class TimingMetric {
public:
    TimingMetric();
    void add(uint64_t duration);
    uint64_t get_count() const;
    uint64_t get_sum() const;
    double compute_percentile(double percentile) const;
    uint64_t get_max() const;

private:
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> sum_{0};
    std::atomic<uint64_t> max_{0};
    std::vector<uint64_t> bucket_limits_;
    std::atomic<uint64_t> buckets_[109]; // == HistogramBucketMapper::BucketCount(). TODO: move to constexpr with cpp20
    size_t num_buckets_;

    size_t find_bucket(uint64_t value) const;

    static void update_max(std::atomic<uint64_t>& max, uint64_t value);
};

size_t METRICS_COMMON[] = {1, 2};

class Statistics {
public:
    void record_time(const std::string& name, uint64_t duration, uint32_t count = 1);
    void record_count(const std::string& name, uint64_t delta);
    std::string generate_report_and_reset() const;

private:
    mutable std::shared_mutex timings_mutex_;
    std::unordered_map<std::string, std::unique_ptr<TimingMetric>> timing_metrics_;

    mutable std::shared_mutex counts_mutex_;
    std::unordered_map<std::string, std::unique_ptr<CountMetric>> count_metrics_;
};

extern Statistics g_statistics;
