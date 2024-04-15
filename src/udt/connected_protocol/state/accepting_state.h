#ifndef UDT_CONNECTED_PROTOCOL_STATE_ACCEPTING_STATE_H_
#define UDT_CONNECTED_PROTOCOL_STATE_ACCEPTING_STATE_H_

#include <boost/chrono.hpp>
#include <boost/log/trivial.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <memory>

#include "udt/connected_protocol/state/base_state.h"
#include "udt/connected_protocol/state/closed_state.h"
#include "udt/connected_protocol/state/connected_state.h"
#include "udt/connected_protocol/state/policy/response_connection_policy.h"

namespace connected_protocol {
namespace state {

/**
 * @brief 接受状态类模板
 *
 * 该类是接受状态的实现，继承自BaseState类，并使用std::enable_shared_from_this来支持共享指针。
 *
 * @tparam Protocol 协议类型
 */
template <class Protocol>
class AcceptingState
    : public BaseState<Protocol>,
      public std::enable_shared_from_this<AcceptingState<Protocol>> {
 public:
  using Ptr = std::shared_ptr<AcceptingState>;
  using SocketSession = typename Protocol::socket_session;
  using ConnectionDatagram = typename Protocol::ConnectionDatagram;
  using ConnectionDatagramPtr = std::shared_ptr<ConnectionDatagram>;
  using ClosedState = typename state::ClosedState<Protocol>;

  using Clock = typename Protocol::clock;
  using Timer = typename Protocol::timer;
  using TimePoint = typename Protocol::time_point;

  using ConnectedState = typename state::ConnectedState<
      Protocol, typename policy::ResponseConnectionPolicy<Protocol>>;

 public:
  /**
   * @brief 创建AcceptingState对象的静态方法
   *
   * @param p_session SocketSession指针
   * @return Ptr AcceptingState的共享指针
   */
  static Ptr Create(typename SocketSession::Ptr p_session) {
    return Ptr(new AcceptingState(std::move(p_session)));
  }

  /**
   * @brief 析构函数
   */
  virtual ~AcceptingState() {}

  /**
   * @brief 获取状态类型
   *
   * @return typename BaseState<Protocol>::type 状态类型
   */
  virtual typename BaseState<Protocol>::type GetType() {
    return this->ACCEPTING;
  }

  /**
   * @brief 初始化函数
   *
   * 设置会话的起始时间戳。
   */
  virtual void Init() {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    p_session->set_start_timestamp(Clock::now());
  }

  /**
   * @brief 关闭函数
   *
   * 关闭会话，切换到ClosedState状态，停止定时器，并解绑会话。
   */
  void Close() {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    auto self = this->shared_from_this();
    // 切换到ClosedState状态
    p_session->ChangeState(ClosedState::Create(this->get_io_service()));

    // 停止定时器
    boost::system::error_code timer_ec;
    timeout_timer_.cancel(timer_ec);

    // 解绑会话
    p_session->Unbind();
  }

  /**
   * @brief 处理连接数据报函数
   *
   * @param p_connection_dgr 连接数据报指针
   */
  virtual void OnConnectionDgr(ConnectionDatagramPtr p_connection_dgr) {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    auto& header = p_connection_dgr->header();
    auto& payload = p_connection_dgr->payload();
    uint32_t receive_cookie = payload.syn_cookie();
    uint32_t client_socket = payload.socket_id();

    auto self = this->shared_from_this();
    if (receive_cookie == p_session->syn_cookie() &&
        client_socket == p_session->remote_socket_id()) {
      // 第一次握手响应
      if (p_session->max_window_flow_size() == 0) {
        p_session->get_p_connection_info()->set_packet_data_size(std::min(
            static_cast<uint32_t>(Protocol::MTU),
            payload.maximum_packet_size() - Protocol::PACKET_SIZE_CORRECTION));
        p_session->set_max_window_flow_size(
            std::min(static_cast<uint32_t>(Protocol::MAXIMUM_WINDOW_FLOW_SIZE),
                     payload.maximum_window_flow_size()));
        p_session->set_window_flow_size(p_session->max_window_flow_size());
        p_session->set_init_packet_seq_num(
            payload.initial_packet_sequence_number());
        p_session->get_p_packet_seq_gen()->set_current(
            p_session->init_packet_seq_num());
        p_session->ChangeState(ConnectedState::Create(p_session));
        boost::system::error_code timer_ec;
        timeout_timer_.cancel(timer_ec);
      }

      // 回复握手响应
      header.set_destination_socket(p_session->remote_socket_id());
      payload.set_version(ConnectionDatagram::Payload::FORTH);
      payload.set_socket_type(ConnectionDatagram::Payload::STREAM);
      payload.set_connection_type(ConnectionDatagram::Payload::FIRST_RESPONSE);
      payload.set_initial_packet_sequence_number(
          p_session->get_p_packet_seq_gen()->current());
      payload.set_syn_cookie(p_session->syn_cookie());
      payload.set_maximum_packet_size(
          p_session->connection_info().packet_data_size() +
          Protocol::PACKET_SIZE_CORRECTION);
      payload.set_maximum_window_flow_size(p_session->max_window_flow_size());
      payload.set_socket_id(p_session->socket_id());
    }

    p_session->AsyncSendControlPacket(
        *p_connection_dgr, ConnectionDatagram::Header::CONNECTION,
        ConnectionDatagram::Header::NO_ADDITIONAL_INFO,
        [self, p_connection_dgr](const boost::system::error_code&,
                                 std::size_t) {});
  }

 private:
  /**
   * @brief 私有构造函数
   *
   * @param p_session SocketSession指针
   */
  AcceptingState(typename SocketSession::Ptr p_session)
      : BaseState<Protocol>(p_session->get_io_service()),
        p_session_(p_session),
        timeout_timer_(p_session->get_io_service()) {}

  /**
   * @brief 启动超时定时器
   */
  void StartTimeoutTimer() {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    timeout_timer_.expires_from_now(
        boost::chrono::seconds(p_session->timeout_delay()));

    timeout_timer_.async_wait(boost::bind(&AcceptingState::HandleTimeoutTimer,
                                          this->shared_from_this(), _1));
  }

  /**
   * @brief 处理超时定时器
   *
   * @param ec 错误码
   */
  void HandleTimeoutTimer(const boost::system::error_code& ec) {
    if (!ec) {
      // 接受超时
      Close();
    }
  }

 private:
  std::weak_ptr<SocketSession> p_session_;
  Timer timeout_timer_;
};

}  // namespace state
}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_STATE_ACCEPTING_STATE_H_
