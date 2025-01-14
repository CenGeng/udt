#ifndef UDT_CONNECTED_PROTOCOL_ACCEPTOR_SESSION_H_
#define UDT_CONNECTED_PROTOCOL_ACCEPTOR_SESSION_H_

#include <boost/asio/detail/op_queue.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <boost/uuid/sha1.hpp>
#include <chrono>
#include <memory>

#include "udt/common/error/error.h"
#include "udt/connected_protocol/io/accept_op.h"
#include "udt/connected_protocol/state/accepting_state.h"
#include "udt/connected_protocol/state/base_state.h"
#include "udt/connected_protocol/state/closed_state.h"

namespace connected_protocol {

/**
 * @brief AcceptorSession类是一个模板类，用于表示接收器会话。
 * @tparam Protocol 表示协议类型。
 */
template <class Protocol>
class AcceptorSession {
 private:
  using TimePoint = typename Protocol::time_point;
  using Clock = typename Protocol::clock;

 private:
  using SocketSession = typename Protocol::socket_session;
  using SocketSessionPtr = std::shared_ptr<SocketSession>;
  using NextLayerEndpoint = typename Protocol::next_layer_protocol::endpoint;
  using NextLayerEndpointPtr = std::shared_ptr<NextLayerEndpoint>;
  using ConnectionDatagram = typename Protocol::ConnectionDatagram;
  using ConnectionDatagramPtr = std::shared_ptr<ConnectionDatagram>;
  using ClosedState = typename state::ClosedState<Protocol>;
  using AcceptingState = typename state::AcceptingState<Protocol>;
  using AcceptOp = typename io::basic_pending_accept_operation<Protocol>;
  using AcceptOpQueue = boost::asio::detail::op_queue<AcceptOp>;

  using MultiplexerPtr = std::shared_ptr<typename Protocol::multiplexer>;

  using SocketId = uint32_t;
  using RemoteSessionsMap = std::map<SocketId, SocketSessionPtr>;

 public:
  /**
   * @brief 构造函数，创建一个AcceptorSession对象。
   */
  AcceptorSession()
      : p_multiplexer_(nullptr),
        mutex_(),
        accept_ops_(),
        connecting_sessions_(),
        connected_sessions_(),
        previous_connected_sessions_(),
        listening_(false),
        max_pending_connections_(boost::asio::socket_base::max_connections),
        start_time_point_(Clock::now()) {}

  /**
   * @brief 析构函数，销毁AcceptorSession对象。
   */
  ~AcceptorSession() { StopListen(); }

  /**
   * @brief 设置Multiplexer指针。
   * @param p_multiplexer 指向Multiplexer的智能指针。
   */
  void set_p_multiplexer(MultiplexerPtr p_multiplexer) {
    p_multiplexer_ = std::move(p_multiplexer);
  }

  /**
   * @brief 判断AcceptorSession是否正在监听连接。
   * @return 如果正在监听连接，则返回true；否则返回false。
   */
  bool IsListening() { return listening_; }

  /**
   * @brief 开始监听连接。
   * @param backlog 监听队列的最大长度。
   * @param ec 用于返回错误码的引用。
   */
  void Listen(int backlog, boost::system::error_code& ec) {
    boost::recursive_mutex::scoped_lock lock(mutex_);
    if (!listening_) {
      listening_ = true;
      max_pending_connections_ =
          static_cast<uint32_t>(backlog >= 0 ? backlog : 0);
      ec.assign(::common::error::success,
                ::common::error::get_error_category());
    } else {
      ec.assign(::common::error::device_or_resource_busy,
                ::common::error::get_error_category());
    }
  }

  /**
   * @brief 停止监听连接。
   */
  void StopListen() { listening_ = false; }

  /**
   * @brief 获取本地终端点。
   * @param ec 用于返回错误码的引用。
   * @return 返回本地终端点。
   */
  NextLayerEndpoint next_local_endpoint(boost::system::error_code& ec) {
    boost::recursive_mutex::scoped_lock lock(mutex_);
    if (!p_multiplexer_) {
      ec.assign(::common::error::no_link,
                ::common::error::get_error_category());
      return;
    }

    return p_multiplexer_->local_endpoint(ec);
  }

  /**
   * @brief 关闭AcceptorSession。
   */
  void Close() {
    boost::recursive_mutex::scoped_lock lock_sessions_accept_ops(mutex_);
    if (!p_multiplexer_) {
      return;
    }

    StopListen();

    for (auto& previous_connected_session_pair : previous_connected_sessions_) {
      previous_connected_session_pair.second->RemoveAcceptor();
    }
    for (auto& connected_session_pair : connected_sessions_) {
      connected_session_pair.second->RemoveAcceptor();
    }
    for (auto& connecting_session_pair : connecting_sessions_) {
      connecting_session_pair.second->RemoveAcceptor();
    }

    boost::system::error_code ec(::common::error::interrupted,
                                 ::common::error::get_error_category());
    CleanAcceptOps(ec);
    p_multiplexer_.reset();
  }

  /**
   * @brief 将AcceptOp推入队列。
   * @param p_accept_op 指向AcceptOp的指针。
   */
  void PushAcceptOp(AcceptOp* p_accept_op) {
    {
      boost::recursive_mutex::scoped_lock lock_accept_ops(mutex_);
      accept_ops_.push(p_accept_op);
    }

    Accept();
  }

  /**
   * @brief 推送连接数据报和远程终端点。
   * @param p_connection_dgr 指向ConnectionDatagram的指针。
   * @param p_remote_endpoint 指向NextLayerEndpoint的指针。
   */
  void PushConnectionDgr(ConnectionDatagramPtr p_connection_dgr,
                         NextLayerEndpointPtr p_remote_endpoint) {
    boost::recursive_mutex::scoped_lock lock(mutex_);
    if (!p_multiplexer_) {
      return;
    }

    if (!listening_) {
      // Not listening, drop packet
      return;
    }

    auto& header = p_connection_dgr->header();
    auto& payload = p_connection_dgr->payload();

    uint32_t receive_cookie = payload.syn_cookie();
    uint32_t destination_socket = header.destination_socket();

    auto remote_socket_id = payload.socket_id();

    SocketSessionPtr p_socket_session(GetSession(remote_socket_id));

    if (!p_socket_session) {
      // First handshake packet
      if (receive_cookie == 0 && destination_socket == 0) {
        HandleFirstHandshakePacket(p_connection_dgr, *p_remote_endpoint);
        return;
      }

      uint32_t server_cookie = GetSynCookie(*p_remote_endpoint);
      if (receive_cookie != server_cookie) {
        // Drop datagram -> cookies not equal
        return;
      }

      // New connection
      boost::system::error_code ec;
      p_socket_session =
          p_multiplexer_->CreateSocketSession(ec, *p_remote_endpoint);
      if (ec) {
        BOOST_LOG_TRIVIAL(trace) << "Error on socket session creation";
      }

      p_socket_session->set_remote_socket_id(remote_socket_id);
      p_socket_session->set_syn_cookie(server_cookie);
      p_socket_session->SetAcceptor(this);
      p_socket_session->ChangeState(AcceptingState::Create(p_socket_session));

      {
        boost::recursive_mutex::scoped_lock lock_sessions(mutex_);
        connecting_sessions_[remote_socket_id] = p_socket_session;
      }
    }

    p_socket_session->PushConnectionDgr(p_connection_dgr);
  }

  /**
   * @brief 通知AcceptorSession有关联的SocketSession发生了状态变化。
   * @param p_subject 指向SocketSession的指针。
   */
  void Notify(typename Protocol::socket_session* p_subject) {
    if (p_subject->GetState() ==
        connected_protocol::state::BaseState<Protocol>::CONNECTED) {
      boost::recursive_mutex::scoped_lock lock_sessions(mutex_);
      auto connecting_it =
          connecting_sessions_.find(p_subject->remote_socket_id());

      if (connecting_it != connecting_sessions_.end()) {
        if (connected_sessions_.size() < max_pending_connections_) {
          connected_sessions_.insert(*connecting_it);
          connecting_sessions_.erase(connecting_it->first);
          Accept();
        } else {
          connecting_it->second->Close();
        }
      }

      return;
    }

    if (p_subject->GetState() ==
        connected_protocol::state::BaseState<Protocol>::CLOSED) {
      boost::recursive_mutex::scoped_lock lock_sessions(mutex_);
      connecting_sessions_.erase(p_subject->remote_socket_id());
      connected_sessions_.erase(p_subject->remote_socket_id());
      previous_connected_sessions_.erase(p_subject->remote_socket_id());

      return;
    }
  }

 private:
  /**
   * @brief 执行Accept操作。
   */
  void Accept() {
    boost::recursive_mutex::scoped_lock lock_sessions_accept_ops(mutex_);
    if (!p_multiplexer_) {
      return;
    }

    if (!connected_sessions_.empty() && !accept_ops_.empty()) {
      boost::system::error_code ec(::common::error::success,
                                   ::common::error::get_error_category());
      auto p_connected_pair = connected_sessions_.begin();
      auto p_socket_session = p_connected_pair->second;

      auto op = std::move(accept_ops_.front());
      accept_ops_.pop();

      auto& peer_socket = op->peer();
      auto& impl = peer_socket.native_handle();
      impl.p_multiplexer = p_multiplexer_;
      impl.p_session = p_socket_session;
      auto do_complete = [op, p_socket_session, ec]() { op->complete(ec); };

      previous_connected_sessions_[p_connected_pair->first] = p_socket_session;
      connected_sessions_.erase(p_connected_pair->first);
      p_multiplexer_->get_io_service().post(std::move(do_complete));
      return;
    }
  }

  /**
   * @brief 清理Accept操作队列。
   * Pop and post accept ops with the given error code
   * @param ec 错误码。
   */
  void CleanAcceptOps(const boost::system::error_code& ec) {
    boost::recursive_mutex::scoped_lock lock_sessions_accept_ops(mutex_);
    if (!p_multiplexer_) {
      return;
    }

    while (!accept_ops_.empty()) {
      auto op = std::move(accept_ops_.front());
      accept_ops_.pop();

      auto do_complete = [op, ec]() { op->complete(ec); };

      p_multiplexer_->get_io_service().post(std::move(do_complete));
    }
  }

  /**
   * @brief 获取指定SocketId的SocketSession。
   * @param remote_socket_id 远程SocketId。
   * @return 返回指定SocketId的SocketSession指针。
   */
  SocketSessionPtr GetSession(SocketId remote_socket_id) {
    boost::recursive_mutex::scoped_lock lock_sessions(mutex_);
    auto connecting_it = connecting_sessions_.find(remote_socket_id);
    if (connecting_it != connecting_sessions_.end()) {
      return connecting_it->second;
    }
    auto connected_it = connected_sessions_.find(remote_socket_id);
    if (connected_it != connected_sessions_.end()) {
      return connected_it->second;
    }
    auto previous_connected_it =
        previous_connected_sessions_.find(remote_socket_id);
    if (previous_connected_it != previous_connected_sessions_.end()) {
      return previous_connected_it->second;
    }

    return nullptr;
  }

  /**
   * @brief 处理第一个握手数据报。
   * @param p_connection_dgr 指向ConnectionDatagram的指针。
   * @param next_remote_endpoint 远程终端点。
   */
  void HandleFirstHandshakePacket(
      ConnectionDatagramPtr p_connection_dgr,
      const NextLayerEndpoint& next_remote_endpoint) {
    boost::recursive_mutex::scoped_lock lock_sessions(mutex_);
    if (!p_multiplexer_) {
      return;
    }

    auto& header = p_connection_dgr->header();
    auto& payload = p_connection_dgr->payload();

    header.set_destination_socket(payload.socket_id());
    payload.set_version(0);
    payload.set_socket_type(ConnectionDatagram::Payload::STREAM);
    payload.set_initial_packet_sequence_number(0);
    payload.set_syn_cookie(GetSynCookie(next_remote_endpoint));
    payload.set_maximum_packet_size(Protocol::MTU);
    payload.set_maximum_window_flow_size(Protocol::MAXIMUM_WINDOW_FLOW_SIZE);
    payload.set_socket_id(0);

    p_multiplexer_->AsyncSendControlPacket(
        *p_connection_dgr, next_remote_endpoint,
        [p_connection_dgr](const boost::system::error_code&, std::size_t) {});
  }

  /**
   * @brief 获取SynCookie。
   * @param next_remote_endpoint 远程终端点。
   * @return 返回SynCookie。
   */
  uint32_t GetSynCookie(const NextLayerEndpoint& next_remote_endpoint) {
    // detail namespace used here
    boost::uuids::detail::sha1 sha1;

    uint32_t hash[5];
    std::stringstream text_str;
    text_str << next_remote_endpoint.address().to_string() << ":"
             << boost::chrono::duration_cast<boost::chrono::minutes>(
                    start_time_point_.time_since_epoch())
                    .count();
    std::string text(text_str.str());
    sha1.process_bytes(text.c_str(), text.size());
    sha1.get_digest(hash);

    // Get first 32 bits of hash
    return hash[0];
  }

 private:
  MultiplexerPtr p_multiplexer_;           ///< 多路复用器指针。
  boost::recursive_mutex mutex_;           ///< 递归互斥锁。
  AcceptOpQueue accept_ops_;               ///< 接受操作队列。
  RemoteSessionsMap connecting_sessions_;  ///< 连接中的会话映射。
  RemoteSessionsMap connected_sessions_;   ///< 已连接的会话映射。
  RemoteSessionsMap previous_connected_sessions_;  ///< 先前连接的会话映射。
  bool listening_;                                 ///< 是否正在监听。
  uint32_t max_pending_connections_;               ///< 最大挂起连接数。
  TimePoint start_time_point_;                     ///< 开始时间点。
};

}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_ACCEPTOR_SESSION_H_
