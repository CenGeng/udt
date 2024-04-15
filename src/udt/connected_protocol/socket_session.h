#ifndef UDT_CONNECTED_PROTOCOL_SOCKET_SESSION_H_
#define UDT_CONNECTED_PROTOCOL_SOCKET_SESSION_H_

#include <algorithm>
#include <boost/asio/io_service.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <set>

#include "udt/connected_protocol/cache/connection_info.h"
#include "udt/connected_protocol/cache/connections_info_manager.h"
#include "udt/connected_protocol/io/connect_op.h"
#include "udt/connected_protocol/io/read_op.h"
#include "udt/connected_protocol/io/write_op.h"
#include "udt/connected_protocol/logger/log_entry.h"
#include "udt/connected_protocol/sequence_generator.h"
#include "udt/connected_protocol/state/base_state.h"
#include "udt/connected_protocol/state/closed_state.h"

namespace connected_protocol {

/**
 * @brief SocketSession类是一个模板类，用于表示连接的会话。
 * 它继承自std::enable_shared_from_this，以支持共享指针的使用。
 * SocketSession类提供了管理连接会话的各种功能和操作。
 *
 * @tparam Protocol 连接协议类型
 */
template <class Protocol>
class SocketSession
    : public std::enable_shared_from_this<SocketSession<Protocol>> {
 private:
  using Endpoint = typename Protocol::endpoint;
  using EndpointPtr = std::shared_ptr<Endpoint>;
  using NextLayerEndpoint = typename Protocol::next_layer_protocol::endpoint;

 private:
  using MultiplexerPtr = std::shared_ptr<typename Protocol::multiplexer>;
  using FlowPtr = std::shared_ptr<typename Protocol::flow>;
  using AcceptorSession = typename Protocol::acceptor_session;
  using ConnectionInfo = cache::ConnectionInfo;
  using ConnectionInfoPtr = cache::ConnectionInfo::Ptr;
  using ClosedState = state::ClosedState<Protocol>;
  using BaseStatePtr =
      typename connected_protocol::state::BaseState<Protocol>::Ptr;

 private:
  using TimePoint = typename Protocol::time_point;
  using Clock = typename Protocol::clock;
  using Timer = typename Protocol::timer;
  using Logger = typename Protocol::logger;

 private:
  using ConnectionDatagram = typename Protocol::ConnectionDatagram;
  using ConnectionDatagramPtr = std::shared_ptr<ConnectionDatagram>;
  using ControlDatagram = typename Protocol::GenericControlDatagram;
  using ControlHeader = typename ControlDatagram::Header;
  using SendDatagram = typename Protocol::SendDatagram;
  using DataDatagram = typename Protocol::DataDatagram;
  using AckDatagram = typename Protocol::AckDatagram;
  using AckOfAckDatagram = typename Protocol::AckOfAckDatagram;
  using PacketSequenceNumber = uint32_t;
  using SocketId = uint32_t;

 public:
  using Ptr = std::shared_ptr<SocketSession>;

 public:
  /**
   * @brief 创建SocketSession对象的静态方法。
   *
   * @param p_multiplexer 多路复用器指针
   * @param p_flow 流指针
   * @return Ptr SocketSession对象的智能指针
   */
  static Ptr Create(MultiplexerPtr p_multiplexer, FlowPtr p_flow) {
    Ptr p_session(
        new SocketSession(std::move(p_multiplexer), std::move(p_flow)));
    p_session->Init();

    return p_session;
  }

  ~SocketSession() {}

  /**
   * @brief 设置下一个远程端点。
   *
   * @param next_remote_ep 下一个远程端点
   */
  void set_next_remote_endpoint(const NextLayerEndpoint& next_remote_ep) {
    if (next_remote_endpoint_ == NextLayerEndpoint()) {
      next_remote_endpoint_ = next_remote_ep;
      p_connection_info_cache_ =
          connections_info_manager_.GetConnectionInfo(next_remote_ep);
      auto connection_cache = p_connection_info_cache_.lock();
      if (connection_cache) {
        connection_info_ = *connection_cache;
      }
    }
  }

  /**
   * @brief 获取下一个远程端点。
   *
   * @return const NextLayerEndpoint& 下一个远程端点
   */
  const NextLayerEndpoint& next_remote_endpoint() {
    return next_remote_endpoint_;
  }

  /**
   * @brief 设置下一个本地端点。
   *
   * @param next_local_ep 下一个本地端点
   */
  void set_next_local_endpoint(const NextLayerEndpoint& next_local_ep) {
    if (next_local_endpoint_ == NextLayerEndpoint()) {
      next_local_endpoint_ = next_local_ep;
    }
  }

  /**
   * @brief 获取下一个本地端点。
   *
   * @return const NextLayerEndpoint& 下一个本地端点
   */
  const NextLayerEndpoint& next_local_endpoint() {
    return next_local_endpoint_;
  }

  /**
   * @brief 检查会话是否已关闭。
   *
   * @return bool 如果会话已关闭，则返回true；否则返回false
   */
  bool IsClosed() {
    auto p_state = p_state_;
    return state::BaseState<Protocol>::CLOSED == p_state->GetType();
  }

  /**
   * @brief 关闭会话。
   */
  void Close() {
    auto p_state = p_state_;
    p_state->Close();
  }

  /**
   * @brief 推送读操作。
   *
   * @param read_op 读操作指针
   */
  void PushReadOp(io::basic_pending_stream_read_operation<Protocol>* read_op) {
    auto p_state = p_state_;
    p_state->PushReadOp(read_op);
  }

  /**
   * @brief 推送写操作。
   *
   * @param write_op 写操作指针
   */
  void PushWriteOp(io::basic_pending_write_operation* write_op) {
    auto p_state = p_state_;
    p_state->PushWriteOp(write_op);
  }

  /**
   * @brief 推送连接数据报。
   *
   * @param p_connection_dgr 连接数据报指针
   */
  void PushConnectionDgr(ConnectionDatagramPtr p_connection_dgr) {
    auto p_state = p_state_;
    p_state->OnConnectionDgr(p_connection_dgr);
  }

  /**
   * @brief 推送控制数据报。
   *
   * @param p_control_dgr 控制数据报指针
   */
  void PushControlDgr(ControlDatagram* p_control_dgr) {
    auto p_state = p_state_;
    p_state->OnControlDgr(p_control_dgr);
  }

  /**
   * @brief 推送数据数据报。
   *
   * @param p_datagram 数据数据报指针
   */
  void PushDataDgr(DataDatagram* p_datagram) {
    auto p_state = p_state_;
    p_state->OnDataDgr(p_datagram);
  }

  /**
   * @brief 检查是否有待发送的数据包。
   *
   * @return bool 如果有待发送的数据包，则返回true；否则返回false
   */
  bool HasPacketToSend() {
    auto p_state = p_state_;
    return p_state->HasPacketToSend();
  }

  /**
   * @brief 获取下一个计划发送的数据包。
   *
   * @return SendDatagram* 下一个计划发送的数据包指针
   */
  SendDatagram* NextScheduledPacket() {
    auto p_state = p_state_;
    return p_state->NextScheduledPacket();
  }

  /**
   * @brief 获取下一个计划发送数据包的时间。
   *
   * @return boost::chrono::nanoseconds 下一个计划发送数据包的时间
   */
  boost::chrono::nanoseconds NextScheduledPacketTime() {
    auto p_state = p_state_;
    return p_state->NextScheduledPacketTime();
  }

  /**
   * @brief 异步发送控制数据包（高优先级）。
   *
   * @tparam Datagram 数据包类型
   * @tparam Handler 完成处理器类型
   * @param datagram 数据包
   * @param type 控制数据包类型
   * @param additional_info 附加信息
   * @param handler 完成处理器
   */
  template <class Datagram, class Handler>
  void AsyncSendControlPacket(Datagram& datagram,
                              typename ControlHeader::type type,
                              uint32_t additional_info, Handler handler) {
    FillControlHeader(&(datagram.header()), type, additional_info);
    p_multiplexer_->AsyncSendControlPacket(datagram, next_remote_endpoint_,
                                           handler);
  }

  /**
   * @brief 异步发送数据包。
   *
   * @tparam Datagram 数据包类型
   * @tparam Handler 完成处理器类型
   * @param p_datagram 数据包指针
   * @param handler 完成处理器
   */
  template <class Datagram, class Handler>
  void AsyncSendPacket(Datagram* p_datagram, Handler handler) {
    p_multiplexer_->AsyncSendDataPacket(p_datagram, next_remote_endpoint_,
                                        handler);
  }

  /**
   * @brief 异步发送数据包。
   */
  void AsyncSendPackets() {
    p_flow_->RegisterNewSocket(this->shared_from_this());
  }

  /**
   * @brief 设置接收器会话。
   *
   * @param p_acceptor 接收器会话指针
   */
  void SetAcceptor(AcceptorSession* p_acceptor) {
    boost::recursive_mutex::scoped_lock lock(acceptor_mutex_);
    p_acceptor_ = p_acceptor;
  }

  /**
   * @brief 移除接收器会话。
   */
  void RemoveAcceptor() {
    boost::recursive_mutex::scoped_lock lock(acceptor_mutex_);
    p_acceptor_ = nullptr;
  }

  /**
   * @brief 更改会话的当前状态。
   *
   * @param p_new_state 新状态指针
   */
  void ChangeState(BaseStatePtr p_new_state) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    auto p_state = p_state_;
    if (p_state) {
      p_state->Stop();
    }
    p_state_ = std::move(p_new_state);
    p_state_->Init();
    NotifyAcceptor();
  }

  /**
   * @brief 解绑会话。
   */
  void Unbind() {
    boost::system::error_code ec;
    logger_timer_.cancel(ec);
    p_multiplexer_->RemoveSocketSession(next_remote_endpoint_, socket_id_);
  }

  /**
   * @brief 获取会话的当前状态。
   *
   * @return typename connected_protocol::state::BaseState<Protocol>::type
   * 会话的当前状态
   */
  typename connected_protocol::state::BaseState<Protocol>::type GetState() {
    return p_state_->GetType();
  }

  /**
   * @brief 获取会话的套接字ID。
   *
   * @return SocketId 套接字ID
   */
  SocketId socket_id() const { return socket_id_; }

  /**
   * @brief 设置会话的套接字ID。
   *
   * @param socket_id 套接字ID
   */
  void set_socket_id(SocketId socket_id) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    socket_id_ = socket_id;
  }

  /**
   * @brief 获取远程套接字ID。
   *
   * @return SocketId 远程套接字ID
   */
  SocketId remote_socket_id() const { return remote_socket_id_; }

  /**
   * @brief 设置远程套接字ID。
   *
   * @param remote_socket_id 远程套接字ID
   */
  void set_remote_socket_id(SocketId remote_socket_id) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    remote_socket_id_ = remote_socket_id;
  }

  /**
   * @brief 获取SYN cookie。
   *
   * @return uint32_t SYN cookie
   */
  uint32_t syn_cookie() const { return syn_cookie_; }

  /**
   * @brief 设置SYN cookie。
   *
   * @param syn_cookie SYN cookie
   */
  void set_syn_cookie(uint32_t syn_cookie) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    syn_cookie_ = syn_cookie;
  }

  /**
   * @brief 获取消息序列号生成器指针。
   *
   * @return SequenceGenerator* 消息序列号生成器指针
   */
  SequenceGenerator* get_p_message_seq_gen() { return &message_seq_gen_; }

  /**
   * @brief 获取消息序列号生成器的常量引用。
   *
   * @return const SequenceGenerator& 消息序列号生成器的常量引用
   */
  const SequenceGenerator& message_seq_gen() const { return message_seq_gen_; }

  /**
   * @brief 获取ACK序列号生成器指针。
   *
   * @return SequenceGenerator* ACK序列号生成器指针
   */
  SequenceGenerator* get_p_ack_seq_gen() { return &ack_seq_gen_; }

  /**
   * @brief 获取ACK序列号生成器的常量引用。
   *
   * @return const SequenceGenerator& ACK序列号生成器的常量引用
   */
  const SequenceGenerator& ack_seq_gen() const { return ack_seq_gen_; }

  /**
   * @brief 获取数据包序列号生成器指针。
   *
   * @return SequenceGenerator* 数据包序列号生成器指针
   */
  SequenceGenerator* get_p_packet_seq_gen() { return &packet_seq_gen_; }

  /**
   * @brief 获取数据包序列号生成器的常量引用。
   *
   * @return const SequenceGenerator& 数据包序列号生成器的常量引用
   */
  const SequenceGenerator& packet_seq_gen() const { return packet_seq_gen_; }

  /**
   * @brief 设置超时延迟。
   *
   * @param delay 超时延迟
   */
  void set_timeout_delay(uint32_t delay) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    timeout_delay_ = delay;
  }

  /**
   * @brief 获取超时延迟。
   *
   * @return uint32_t 超时延迟
   */
  uint32_t timeout_delay() const { return timeout_delay_; }

  /**
   * @brief 设置会话的起始时间戳。
   *
   * @param start 起始时间戳
   */
  void set_start_timestamp(const TimePoint& start) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    start_timestamp_ = start;
  }

  /**
   * @brief 获取会话的起始时间戳。
   *
   * @return const TimePoint& 起始时间戳
   */
  const TimePoint& start_timestamp() const { return start_timestamp_; }

  /**
   * @brief 获取最大窗口流大小。
   *
   * @return uint32_t 最大窗口流大小
   */
  uint32_t max_window_flow_size() const { return max_window_flow_size_; }

  /**
   * @brief 设置最大窗口流大小。
   *
   * @param max_window_flow_size 最大窗口流大小
   */
  void set_max_window_flow_size(uint32_t max_window_flow_size) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    max_window_flow_size_ = max_window_flow_size;
  }

  /**
   * @brief 获取窗口流大小。
   *
   * @return uint32_t 窗口流大小
   */
  uint32_t window_flow_size() const { return window_flow_size_; }

  /**
   * @brief 设置窗口流大小。
   *
   * @param window_flow_size 窗口流大小
   */
  void set_window_flow_size(uint32_t window_flow_size) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    window_flow_size_ = window_flow_size;
  }

  /**
   * @brief 获取初始数据包序列号。
   *
   * @return uint32_t 初始数据包序列号
   */
  uint32_t init_packet_seq_num() const { return init_packet_seq_num_; }

  /**
   * @brief 设置初始数据包序列号。
   *
   * @param init_packet_seq_num 初始数据包序列号
   */
  void set_init_packet_seq_num(uint32_t init_packet_seq_num) {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    init_packet_seq_num_ = init_packet_seq_num;
  }

  /**
   * @brief 获取连接信息。
   *
   * @return const ConnectionInfo& 连接信息
   */
  const ConnectionInfo& connection_info() const { return connection_info_; }

  /**
   * @brief 获取连接信息指针。
   *
   * @return ConnectionInfo* 连接信息指针
   */
  ConnectionInfo* get_p_connection_info() { return &connection_info_; }

  /**
   * @brief 更新缓存的连接信息。
   */
  void UpdateCacheConnection() {
    boost::recursive_mutex::scoped_lock lock_session(mutex_);
    auto connection_cache = p_connection_info_cache_.lock();
    if (connection_cache) {
      connection_cache->Update(connection_info_);
    }
  }

  /**
   * @brief 获取IO服务。
   *
   * @return boost::asio::io_service& IO服务
   */
  boost::asio::io_service& get_io_service() {
    return p_multiplexer_->get_io_service();
  }

 private:
  /**
   * @brief SocketSession构造函数。
   *
   * @param p_multiplexer 多路复用器指针
   * @param p_fl 流指针
   */
  SocketSession(MultiplexerPtr p_multiplexer, FlowPtr p_fl)
      : mutex_(),
        p_multiplexer_(std::move(p_multiplexer)),
        p_flow_(std::move(p_fl)),
        acceptor_mutex_(),
        p_acceptor_(nullptr),
        p_state_(ClosedState::Create(p_multiplexer_->get_io_service())),
        socket_id_(0),
        remote_socket_id_(0),
        next_local_endpoint_(),
        next_remote_endpoint_(),
        syn_cookie_(0),
        max_window_flow_size_(0),
        window_flow_size_(0),
        init_packet_seq_num_(0),
        message_seq_gen_(Protocol::MAX_MSG_SEQUENCE_NUMBER),
        packet_seq_gen_(Protocol::MAX_PACKET_SEQUENCE_NUMBER),
        ack_seq_gen_(Protocol::MAX_ACK_SEQUENCE_NUMBER),
        timeout_delay_(30),
        start_timestamp_(Clock::now()),
        connection_info_(),
        p_connection_info_cache_(),
        logger_timer_(p_multiplexer_->get_io_service()),
        logger_() {}

  /**
   * @brief 初始化函数。
   *
   * 使用多路复用器的本地端点初始化本地端点。
   */
  void Init() {
    // initialize local endpoint with multiplexer's one
    boost::system::error_code ec;
    next_local_endpoint_ = p_multiplexer_->local_endpoint(ec);
    LaunchLoggerTimer();
  }

  /**
   * @brief 启动日志定时器。
   */
  void LaunchLoggerTimer() {
    if (Logger::ACTIVE) {
      ResetLog();
      logger_timer_.expires_from_now(
          boost::chrono::milliseconds(Logger::FREQUENCY));
      logger_timer_.async_wait(boost::bind(&SocketSession::LoggerTimerHandler,
                                           this->shared_from_this(), _1));
    }
  }

  /**
   * @brief 日志定时器处理函数。
   *
   * @param ec 错误代码
   */
  void LoggerTimerHandler(const boost::system::error_code& ec) {
    if (!ec) {
      logger::LogEntry log_entry;
      Log(&log_entry);
      p_state_->Log(&log_entry);
      p_flow_->Log(&log_entry);
      p_multiplexer_->Log(&log_entry);

      logger_.Log(log_entry);
      LaunchLoggerTimer();
    }
  }

  /**
   * @brief 日志函数。
   *
   * @param p_log 日志条目指针
   */
  void Log(connected_protocol::logger::LogEntry* p_log) {
    p_log->sending_period = connection_info_.sending_period();
    p_log->cc_window_flow_size = connection_info_.window_flow_size();
    p_log->remote_window_flow_size = window_flow_size_;
    p_log->remote_arrival_speed = connection_info_.packet_arrival_speed();
    p_log->remote_estimated_link_capacity =
        connection_info_.estimated_link_capacity();
    p_log->rtt = connection_info_.rtt().count();
    p_log->rtt_var = connection_info_.rtt_var().count();
    p_log->ack_period = connection_info_.ack_period().count();
  }

  /**
   * @brief 重置日志。
   */
  void ResetLog() {
    p_flow_->ResetLog();
    p_multiplexer_->ResetLog();
    p_state_->ResetLog();
  }
  /**
   * @brief 填充控制头部。
   *
   * @param p_control_header 控制头部指针
   * @param type 控制头部类型
   * @param additional_info 附加信息
   */
  void FillControlHeader(ControlHeader* p_control_header,
                         typename ControlHeader::type type,
                         uint32_t additional_info) {
    p_control_header->set_flags(type);
    p_control_header->set_additional_info(additional_info);
    p_control_header->set_destination_socket(remote_socket_id_);
    p_control_header->set_timestamp(static_cast<uint32_t>(
        boost::chrono::duration_cast<boost::chrono::microseconds>(
            Clock::now() - start_timestamp_)
            .count()));
  }

  /**
   * @brief 通知接收器。
   */
  void NotifyAcceptor() {
    boost::recursive_mutex::scoped_lock lock(acceptor_mutex_);
    if (p_acceptor_) {
      p_acceptor_->Notify(this);
    }
  }

 private:
  boost::recursive_mutex mutex_;              ///< 递归互斥锁
  MultiplexerPtr p_multiplexer_;              ///< 多路复用器指针
  FlowPtr p_flow_;                            ///< 流指针
  boost::recursive_mutex acceptor_mutex_;     ///< 接收器互斥锁
  AcceptorSession* p_acceptor_;               ///< 接收器会话指针
  BaseStatePtr p_state_;                      ///< 基础状态指针
  SocketId socket_id_;                        ///< 套接字ID
  SocketId remote_socket_id_;                 ///< 远程套接字ID
  NextLayerEndpoint next_local_endpoint_;     ///< 下一层本地端点
  NextLayerEndpoint next_remote_endpoint_;    ///< 下一层远程端点
  uint32_t syn_cookie_;                       ///< SYN cookie
  uint32_t max_window_flow_size_;             ///< 最大窗口流大小
  uint32_t window_flow_size_;                 ///< 窗口流大小
  PacketSequenceNumber init_packet_seq_num_;  ///< 初始包序列号
  SequenceGenerator message_seq_gen_;         ///< 消息序列生成器
  SequenceGenerator packet_seq_gen_;          ///< 包序列生成器
  SequenceGenerator ack_seq_gen_;             ///< 确认序列生成器
  uint32_t timeout_delay_;                    ///< 超时延迟（秒）
  TimePoint start_timestamp_;                 ///< 开始时间戳
  ConnectionInfo connection_info_;            ///< 连接信息
  std::weak_ptr<ConnectionInfo> p_connection_info_cache_;  ///< 连接信息缓存指针
  Timer logger_timer_;  ///< 日志记录器计时器
  Logger logger_;       ///< 日志记录器

 private:
  static cache::ConnectionsInfoManager<Protocol> connections_info_manager_;
};

template <class Protocol>
cache::ConnectionsInfoManager<Protocol>
    SocketSession<Protocol>::connections_info_manager_;

}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_SOCKET_SESSION_H_
