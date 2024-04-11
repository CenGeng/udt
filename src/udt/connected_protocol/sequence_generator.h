#ifndef UDT_CONNECTED_PROTOCOL_SEQUENCE_GENERATOR_H_
#define UDT_CONNECTED_PROTOCOL_SEQUENCE_GENERATOR_H_

#include <cstdint>
#include <cstdlib>

#include <chrono>

#include <boost/chrono.hpp>
#include <boost/log/trivial.hpp>

#include <boost/thread/mutex.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

namespace connected_protocol {

/**
 * @brief 序列号生成器类
 * 
 * 该类用于生成序列号，并提供一些操作序列号的方法。
 */
class SequenceGenerator {
 public:
  using SeqNumber = uint32_t;

 public:
  /**
   * @brief 构造函数
   * 
   * @param max_value 序列号的最大值
   */
  SequenceGenerator(SeqNumber max_value)
      : mutex_(),
        current_(0),
        max_value_(max_value),
        threshold_compare_(max_value_ >> 1) {
    boost::random::mt19937 gen(static_cast<SeqNumber>(
        boost::chrono::duration_cast<boost::chrono::nanoseconds>(
            boost::chrono::high_resolution_clock::now().time_since_epoch())
            .count()));
    boost::random::uniform_int_distribution<uint32_t> dist(0, max_value_);
    current_ = dist(gen);
  }

  /**
   * @brief 获取上一个序列号
   * 
   * @return 上一个序列号
   */
  SeqNumber Previous() {
    boost::mutex::scoped_lock lock(mutex_);
    current_ = Dec(current_);
    return current_;
  }

  /**
   * @brief 获取下一个序列号
   * 
   * @return 下一个序列号
   */
  SeqNumber Next() {
    boost::mutex::scoped_lock lock(mutex_);
    current_ = Inc(current_);
    return current_;
  }

  /**
   * @brief 设置当前序列号
   * 
   * 如果设置的序列号大于最大值，则将当前序列号设置为0。
   * 
   * @param current 当前序列号
   */
  void set_current(SeqNumber current) {
    boost::mutex::scoped_lock lock(mutex_);
    if (current > max_value_) {
      current_ = 0;
    } else {
      current_ = current;
    }
  }

  /**
   * @brief 获取当前序列号
   * 
   * @return 当前序列号
   */
  SeqNumber current() const {
    boost::mutex::scoped_lock lock(mutex_);
    return current_;
  }

  /**
   * @brief 比较两个序列号的大小
   * 
   * 如果lhs > rhs，则返回正数；如果lhs < rhs，则返回负数。
   * 
   * @param lhs 左操作数
   * @param rhs 右操作数
   * @return 比较结果
   */
  int Compare(SeqNumber lhs, SeqNumber rhs) const {
    return (static_cast<uint32_t>(std::abs(static_cast<int>(lhs - rhs))) <
            threshold_compare_)
               ? (lhs - rhs)
               : (rhs - lhs);
  }

  /**
   * @brief 递增序列号
   * 
   * 如果序列号已经达到最大值，则将其重置为0。
   * 
   * @param seq_num 序列号
   * @return 递增后的序列号
   */
  SeqNumber Inc(SeqNumber seq_num) const {
    if (seq_num == max_value_) {
      return 0;
    } else {
      return seq_num + 1;
    }
  }

  /**
   * @brief 递减序列号
   * 
   * 如果序列号已经为0，则将其重置为最大值。
   * 
   * @param seq_num 序列号
   * @return 递减后的序列号
   */
  SeqNumber Dec(SeqNumber seq_num) const {
    if (seq_num == 0) {
      return max_value_;
    } else {
      return seq_num - 1;
    }
  }

  /**
   * @brief 计算序列号的长度
   * 
   * @param first 起始序列号
   * @param last 结束序列号
   * @return 序列号的长度
   */
  int32_t SeqLength(int32_t first, int32_t last) const {
    return (first <= last) ? (last - first + 1)
                           : (last - first + max_value_ + 2);
  }

  /**
   * @brief 计算序列号的偏移量
   * 
   * @param first 起始序列号
   * @param last 结束序列号
   * @return 序列号的偏移量
   */
  int32_t SeqOffset(int32_t first, int32_t last) const {
    if (std::abs(first - last) < static_cast<int32_t>(threshold_compare_)) {
      return last - first;
    }

    if (first < last) {
      return last - first - static_cast<int32_t>(max_value_) - 1;
    }

    return last - first + static_cast<int32_t>(max_value_) + 1;
  }

 private:
  mutable boost::mutex mutex_;  ///< 互斥锁，用于保护当前序列号的访问
  SeqNumber current_;           ///< 当前序列号
  SeqNumber max_value_;         ///< 序列号的最大值
  SeqNumber threshold_compare_; ///< 比较阈值，用于判断两个序列号的差值是否小于阈值
};

}  // connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_SEQUENCE_GENERATOR_H_
