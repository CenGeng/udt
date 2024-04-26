#ifndef UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_PACKET_TIME_HISTORY_WINDOW_H_
#define UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_PACKET_TIME_HISTORY_WINDOW_H_

#include <algorithm>
#include <boost/chrono.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/thread/mutex.hpp>
#include <chrono>
#include <cstdint>
#include <numeric>

namespace connected_protocol {
namespace state {
namespace connected {

/**
 * @brief 用于记录数据包到达时间和探测时间间隔的历史窗口类。
 */
class PacketTimeHistoryWindow {
 private:
  using TimePoint =
      boost::chrono::time_point<boost::chrono::high_resolution_clock>;
  using MicrosecUnit = int_least64_t;
  using CircularBuffer = boost::circular_buffer<MicrosecUnit>;

 public:
  /**
   * @brief 构造函数，初始化历史窗口的大小和初始值。
   * @param max_arrival_size 最大到达时间间隔历史记录大小，默认为16。
   * @param max_probe_size 最大探测时间间隔历史记录大小，默认为64。
   */
  PacketTimeHistoryWindow(uint32_t max_arrival_size = 16,
                          uint32_t max_probe_size = 64)
      : arrival_mutex_(),
        max_packet_arrival_speed_size_(max_arrival_size),
        arrival_interval_history_(max_arrival_size),
        last_arrival_(boost::chrono::high_resolution_clock::now()),
        probe_mutex_(),
        max_probe_interval_size_(max_probe_size),
        probe_interval_history_(max_probe_size),
        first_probe_arrival_(boost::chrono::high_resolution_clock::now()) {}

  /**
   * @brief 初始化历史窗口的初始值。
   * @param packet_arrival_speed 数据包到达速度，默认为1000000.0。
   * @param estimated_capacity 预估链路容量，默认为1000.0。
   */
  void Init(double packet_arrival_speed = 1000000.0,
            double estimated_capacity = 1000.0) {
    MicrosecUnit packet_interval(
        static_cast<MicrosecUnit>(ceil(1000000 / packet_arrival_speed)));
    for (uint32_t i = 0; i < max_packet_arrival_speed_size_; ++i) {
      arrival_interval_history_.push_back(packet_interval);
    }
    MicrosecUnit probe_interval(
        static_cast<MicrosecUnit>(ceil(1000000 / estimated_capacity)));
    for (uint32_t i = 0; i < max_probe_interval_size_; ++i) {
      probe_interval_history_.push_back(probe_interval);
    }
  }

  /**
   * @brief 记录数据包到达时间。
   */
  void OnArrival() {
    boost::mutex::scoped_lock lock_arrival(arrival_mutex_);
    TimePoint arrival_time(boost::chrono::high_resolution_clock::now());
    MicrosecUnit delta(DeltaTime(arrival_time, last_arrival_));
    arrival_interval_history_.push_back(delta);
    last_arrival_ = arrival_time;
  }

  /**
   * @brief 记录第一次探测时间。
   */
  void OnFirstProbe() {
    boost::mutex::scoped_lock lock_probe(probe_mutex_);
    first_probe_arrival_ = boost::chrono::high_resolution_clock::now();
  }

  /**
   * @brief 记录第二次探测时间。
   */
  void OnSecondProbe() {
    boost::mutex::scoped_lock lock_probe(probe_mutex_);
    TimePoint arrival_time(boost::chrono::high_resolution_clock::now());

    probe_interval_history_.push_back(
        DeltaTime(arrival_time, first_probe_arrival_));
  }

  /**
   * @brief 获取数据包到达速度。
   * @return 数据包每秒到达的数量。
   */
  double GetPacketArrivalSpeed() {
    boost::mutex::scoped_lock lock_arrival(arrival_mutex_);
    // copy values
    std::vector<MicrosecUnit> sorted_values(arrival_interval_history_.begin(),
                                            arrival_interval_history_.end());

    std::sort(sorted_values.begin(), sorted_values.end());
    MicrosecUnit median(sorted_values[sorted_values.size() / 2]);
    MicrosecUnit low_value(median / 8);
    MicrosecUnit upper_value(median * 8);
    auto it = std::copy_if(
        sorted_values.begin(), sorted_values.end(), sorted_values.begin(),
        [low_value, upper_value](MicrosecUnit current_value) {
          return current_value > low_value && current_value < upper_value;
        });
    // todo change resize
    sorted_values.resize(std::distance(sorted_values.begin(), it));

    if (sorted_values.size() > 8) {
      double sum(
          std::accumulate(sorted_values.begin(), sorted_values.end(), 0.0));
      return (1000000.0 * sorted_values.size()) / sum;
    } else {
      return 0;
    }
  }

  /**
   * @brief 获取预估链路容量。
   * @return 链路每秒传输的数据包数量。
   */
  double GetEstimatedLinkCapacity() {
    boost::mutex::scoped_lock lock_probe(probe_mutex_);
    // copy values
    std::vector<MicrosecUnit> sorted_values(probe_interval_history_.begin(),
                                            probe_interval_history_.end());

    std::sort(sorted_values.begin(), sorted_values.end());
    MicrosecUnit median(sorted_values[sorted_values.size() / 2]);
    MicrosecUnit low_value(median / 8);
    MicrosecUnit upper_value(median * 8);
    auto it = std::copy_if(
        sorted_values.begin(), sorted_values.end(), sorted_values.begin(),
        [low_value, upper_value](MicrosecUnit current_value) {
          return current_value > low_value && current_value < upper_value;
        });
    // todo change resize
    sorted_values.resize(std::distance(sorted_values.begin(), it));

    double sum(
        std::accumulate(sorted_values.begin(), sorted_values.end(), 0.0));

    if (sum == 0) {
      return 0;
    }

    return (1000000.0 * sorted_values.size()) / sum;
  }

 private:
  /**
   * @brief 计算两个时间点之间的时间差。
   * @param t1 第一个时间点。
   * @param t2 第二个时间点。
   * @return 时间差，以微秒为单位。
   */
  MicrosecUnit DeltaTime(const TimePoint &t1, const TimePoint &t2) {
    return boost::chrono::duration_cast<boost::chrono::microseconds>(t1 - t2)
        .count();
  }

 private:
  boost::mutex arrival_mutex_;              // 数据包到达时间互斥锁
  uint32_t max_packet_arrival_speed_size_;  // 最大到达时间间隔历史记录大小
  CircularBuffer arrival_interval_history_;  // 数据包到达时间间隔历史记录
  TimePoint last_arrival_;                   // 上次数据包到达时间

  boost::mutex probe_mutex_;          // 探测时间互斥锁
  uint32_t max_probe_interval_size_;  // 最大探测时间间隔历史记录大小
  CircularBuffer probe_interval_history_;  // 探测时间间隔历史记录
  TimePoint first_probe_arrival_;          // 第一次探测时间
};

}  // namespace connected
}  // namespace state
}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_PACKET_TIME_HISTORY_WINDOW_H_
