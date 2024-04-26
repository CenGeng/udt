#ifndef UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_ACK_HISTORY_WINDOW_H_
#define UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_ACK_HISTORY_WINDOW_H_

#include <algorithm>
#include <boost/asio/high_resolution_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/mutex.hpp>
#include <cstdint>
#include <map>
#include <numeric>
#include <queue>

namespace connected_protocol {
namespace state {
namespace connected {

/**
 * @brief 确认历史窗口类，用于存储和处理确认信息的历史记录
 */
class AckHistoryWindow {
 public:
  using PacketSequenceNumber = uint32_t;
  using AckSequenceNumber = uint32_t;
  using Clock = boost::chrono::high_resolution_clock;
  using TimePoint = boost::chrono::time_point<Clock>;

 public:
  /**
   * @brief 构造函数
   * @param size 窗口大小，默认为1024
   */
  AckHistoryWindow(uint32_t size = 1024)
      : mutex_(),
        current_index_(0),
        oldest_index_(0),
        packet_sequence_numbers_(size),
        ack_sequence_numbers_(size),
        ack_timestamps_(size) {}

  /**
   * @brief 存储确认信息
   * @param ack_num 确认序列号
   * @param packet_num 数据包序列号
   */
  void StoreAck(AckSequenceNumber ack_num, PacketSequenceNumber packet_num) {
    boost::mutex::scoped_lock lock(mutex_);
    uint32_t window_size =
        static_cast<uint32_t>(packet_sequence_numbers_.size());
    ack_sequence_numbers_[current_index_] = ack_num;
    packet_sequence_numbers_[current_index_] = packet_num;
    ack_timestamps_[current_index_] = Clock::now();
    current_index_ = (current_index_ + 1) % window_size;
    if (current_index_ == oldest_index_) {
      oldest_index_ = (oldest_index_ + 1) % window_size;
    }
  }

  /**
   * @brief 确认指定的确认序列号，并返回对应的数据包序列号和往返时间
   * @param ack_seq_num 确认序列号
   * @param p_packet_seq_num 用于存储数据包序列号的指针
   * @param p_rtt 用于存储往返时间的指针
   * @return 如果确认序列号存在，则返回true；否则返回false
   */
  bool Acknowledge(AckSequenceNumber ack_seq_num,
                   PacketSequenceNumber* p_packet_seq_num,
                   boost::chrono::microseconds* p_rtt) {
    boost::mutex::scoped_lock lock(mutex_);
    uint32_t window_size =
        static_cast<uint32_t>(packet_sequence_numbers_.size());
    if (current_index_ >= oldest_index_) {
      for (uint32_t i = oldest_index_, newest_index = current_index_;
           i < newest_index; ++i) {
        if (ack_sequence_numbers_[i] == ack_seq_num) {
          *p_packet_seq_num = packet_sequence_numbers_[i];
          *p_rtt = boost::chrono::duration_cast<boost::chrono::microseconds>(
              Clock::now() - ack_timestamps_[i]);

          // 更新最后一个被确认的确认序列号
          if (i + 1 == current_index_) {
            oldest_index_ = current_index_ = 0;
            packet_sequence_numbers_[current_index_] = 0;
          } else {
            oldest_index_ = (i + 1) % window_size;
          }

          return true;
        }
      }
      // 确认序列号被覆盖
      return false;
    } else {
      for (uint32_t i = oldest_index_, n = current_index_ + window_size; i < n;
           ++i) {
        if (ack_sequence_numbers_[i % window_size] == ack_seq_num) {
          i %= window_size;
          *p_packet_seq_num = packet_sequence_numbers_[i];
          *p_rtt = boost::chrono::duration_cast<boost::chrono::microseconds>(
              Clock::now() - ack_timestamps_[i]);

          // 更新最后一个被确认的确认序列号
          if (i == current_index_) {
            oldest_index_ = current_index_ = 0;
            packet_sequence_numbers_[current_index_] = 0;
          } else {
            oldest_index_ = (i + 1) % window_size;
          }

          return true;
        }
      }
      // 确认序列号被覆盖
      return false;
    }
  }

 private:
  boost::mutex mutex_;
  uint32_t current_index_;
  uint32_t oldest_index_;
  std::vector<PacketSequenceNumber> packet_sequence_numbers_;
  std::vector<AckSequenceNumber> ack_sequence_numbers_;
  std::vector<TimePoint> ack_timestamps_;
};

}  // namespace connected
}  // namespace state
}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_ACK_HISTORY_WINDOW_H_
