#ifndef UDT_CONNECTED_PROTOCOL_FLOW_H_
#define UDT_CONNECTED_PROTOCOL_FLOW_H_

#include <cstdint>

#include <chrono>
#include <set>
#include <memory>

#include <boost/asio/buffer.hpp>

#include <boost/log/trivial.hpp>

#include <boost/bind.hpp>

#include <boost/system/error_code.hpp>
#include <boost/thread/recursive_mutex.hpp>

#include "udt/connected_protocol/logger/log_entry.h"

/**
 * @brief 连接协议的流类
 * 
 * 该类是连接协议的流类，继承自std::enable_shared_from_this<Flow<Protocol>>。
 * 它管理连接协议的流操作，包括发送数据包、注册新的套接字等。
 * 
 * @tparam Protocol 连接协议类型
 */
namespace connected_protocol {

template <class Protocol>
class Flow : public std::enable_shared_from_this<Flow<Protocol>> {
 public:
  using NextEndpointType = typename Protocol::next_layer_protocol::endpoint;
  using Datagram = typename Protocol::SendDatagram;
  using DatagramPtr = std::shared_ptr<Datagram>;

  struct DatagramAddressPair {
    DatagramPtr p_datagram;
    NextEndpointType remote_endpoint;
  };

 private:
  using Timer = typename Protocol::timer;
  using Clock = typename Protocol::clock;
  using TimePoint = typename Protocol::time_point;
  using Logger = typename Protocol::logger;
  using SocketSession = typename Protocol::socket_session;

  /**
   * @brief 比较会话数据包发送时间的函数对象
   * 
   * 该函数对象用于比较两个弱引用指向的 SocketSession 对象的下一个计划发送数据包的时间，
   * 并返回比较结果。如果其中一个 SocketSession 对象已经被销毁，则返回 true。
   * 
   * @tparam SocketSession 弱引用指向的 SocketSession 类型
   */
  struct CompareSessionPacketSendingTime {
    /**
     * @brief 比较两个 SocketSession 对象的下一个计划发送数据包的时间
     * 
     * @param p_lhs 左侧 SocketSession 对象的弱引用
     * @param p_rhs 右侧 SocketSession 对象的弱引用
     * @return true 如果左侧 SocketSession 对象或右侧 SocketSession 对象已经被销毁
     * @return false 如果左侧 SocketSession 对象的下一个计划发送数据包的时间小于右侧 SocketSession 对象的下一个计划发送数据包的时间
     */
    bool operator()(std::weak_ptr<SocketSession> p_lhs,
                    std::weak_ptr<SocketSession> p_rhs) {
      auto p_shared_lhs = p_lhs.lock();
      auto p_shared_rhs = p_rhs.lock();
      if (!p_shared_lhs || !p_shared_rhs) {
        return true;
      }

      return p_shared_lhs->NextScheduledPacketTime() <
             p_shared_rhs->NextScheduledPacketTime();
    }
  };

  using SocketsContainer =
      std::set<std::weak_ptr<SocketSession>, CompareSessionPacketSendingTime>;

 public:
  using Ptr = std::shared_ptr<Flow>;

 public:
  /**
   * @brief 创建一个指向 Flow 对象的智能指针。
   * 
   * @param io_service Boost.Asio 的 io_service 对象引用。
   * @return 指向 Flow 对象的智能指针。
   */
  static Ptr Create(boost::asio::io_service& io_service) {
    return Ptr(new Flow(io_service));
  }

  ~Flow() {}

  void RegisterNewSocket(typename SocketSession::Ptr p_session) {
    {
      boost::recursive_mutex::scoped_lock lock(mutex_);
      auto inserted = socket_sessions_.insert(p_session);
      if (inserted.second) {
        // relaunch timer since there is a new socket
        StopPullSocketQueue();
      }
    }

    StartPullingSocketQueue();
  }

  void Log(connected_protocol::logger::LogEntry* p_log) {
    p_log->flow_sent_count = sent_count_.load();
  }

  void ResetLog() { sent_count_ = 0; }

 private:
  Flow(boost::asio::io_service& io_service)
      : io_service_(io_service),
        mutex_(),
        socket_sessions_(),
        next_packet_timer_(io_service),
        pulling_(false),
        sent_count_(0) {}

  void StartPullingSocketQueue() {
    if (pulling_.load()) {
      return;
    }
    pulling_ = true;
    PullSocketQueue();
  }

  void PullSocketQueue() {
    typename SocketsContainer::iterator p_next_socket_expired_it;

    if (!pulling_.load()) {
      return;
    }

    {
      boost::recursive_mutex::scoped_lock lock_socket_sessions(mutex_);
      if (socket_sessions_.empty()) {
        this->StopPullSocketQueue();
        return;
      }

      p_next_socket_expired_it = socket_sessions_.begin();

      auto p_next_socket_expired = (*p_next_socket_expired_it).lock();
      if (!p_next_socket_expired) {
        io_service_.post(
            boost::bind(&Flow::PullSocketQueue, this->shared_from_this()));
        return;
      }

      auto next_scheduled_packet_interval =
          p_next_socket_expired->NextScheduledPacketTime();

      if (next_scheduled_packet_interval.count() <= 0) {
        // Resend immediatly
        boost::system::error_code ec;
        ec.assign(::common::error::success,
                  ::common::error::get_error_category());
        io_service_.post(boost::bind(&Flow::WaitPullSocketHandler,
                                     this->shared_from_this(), ec));
        return;
      }

      next_packet_timer_.expires_from_now(next_scheduled_packet_interval);
      next_packet_timer_.async_wait(boost::bind(&Flow::WaitPullSocketHandler,
                                                this->shared_from_this(), _1));
    }
  }

  // When no data is available
  void StopPullSocketQueue() {
    pulling_ = false;
    boost::system::error_code ec;
    next_packet_timer_.cancel(ec);
  }

  void WaitPullSocketHandler(const boost::system::error_code& ec) {
    if (ec) {
      StopPullSocketQueue();
      return;
    }

    typename SocketSession::Ptr p_session;
    Datagram* p_datagram;
    {
      boost::recursive_mutex::scoped_lock lock_socket_sessions(mutex_);

      if (socket_sessions_.empty()) {
        StopPullSocketQueue();
        return;
      }

      typename SocketsContainer::iterator p_session_it;
      p_session_it = socket_sessions_.begin();

      p_session = (*p_session_it).lock();

      if (!p_session) {
        socket_sessions_.erase(p_session_it);
        PullSocketQueue();
        return;
      }
      socket_sessions_.erase(p_session_it);
      p_datagram = p_session->NextScheduledPacket();

      if (p_session->HasPacketToSend()) {
        socket_sessions_.insert(p_session);
      }
    }

    if (p_datagram && p_session) {
      if (Logger::ACTIVE) {
        sent_count_ = sent_count_.load() + 1;
      }
      auto self = this->shared_from_this();
      p_session->AsyncSendPacket(
          p_datagram, [p_datagram](const boost::system::error_code& ec,
                                   std::size_t length) {});
    }

    PullSocketQueue();
  }

 private:
  boost::asio::io_service& io_service_;

  boost::recursive_mutex mutex_;
  // sockets list
  SocketsContainer socket_sessions_;

  Timer next_packet_timer_;
  std::atomic<bool> pulling_;
  std::atomic<uint32_t> sent_count_;
};

}  // connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_FLOW_H_
