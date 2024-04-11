#ifndef UDT_CONNECTED_PROTOCOL_SOCKET_ACCEPTOR_SERVICE_H_
#define UDT_CONNECTED_PROTOCOL_SOCKET_ACCEPTOR_SERVICE_H_

#include <boost/asio/async_result.hpp>
#include <boost/asio/io_service.hpp>

#include <boost/thread/thread.hpp>
#include <boost/system/error_code.hpp>

#include "udt/common/error/error.h"

#include "udt/connected_protocol/io/accept_op.h"

namespace connected_protocol {

#include <boost/asio/detail/push_options.hpp>

/**
 * @brief socket_acceptor_service类是一个模板类，用于实现socket的acceptor服务。
 * 它继承自boost::asio::detail::service_base类，并提供了一系列用于操作acceptor的函数和类型定义。
 * @tparam Prococol 协议类型
 */
template <class Prococol>
class socket_acceptor_service : public boost::asio::detail::service_base<
                                    socket_acceptor_service<Prococol>> {
 public:
  using protocol_type = Prococol; // 协议类型
  using endpoint_type = typename protocol_type::endpoint; // 端点类型
  using p_endpoint_type = std::shared_ptr<endpoint_type>; // 端点类型的智能指针
  using resolver_type = typename protocol_type::resolver; // 解析器类型

  using next_socket_type = typename protocol_type::next_layer_protocol::socket; // 下一层协议的socket类型
  using p_next_socket_type = std::shared_ptr<next_socket_type>; // 下一层协议的socket类型的智能指针
  using acceptor_session_type = typename protocol_type::acceptor_session; // 接收器会话类型
  using p_acceptor_session_type = std::shared_ptr<acceptor_session_type>; // 接收器会话类型的智能指针
  using multiplexer = typename protocol_type::multiplexer; // 多路复用器类型
  using p_multiplexer_type = std::shared_ptr<multiplexer>; // 多路复用器类型的智能指针

  struct implementation_type {
    p_multiplexer_type p_multiplexer; // 多路复用器的智能指针
    p_acceptor_session_type p_acceptor; // 接收器会话的智能指针
  };

  using native_handle_type = implementation_type&; // 本地句柄类型
  using native_type = native_handle_type; // 本地类型

 public:
  /**
   * @brief 构造函数，初始化socket_acceptor_service对象。
   * @param io_service IO服务对象
   */
  explicit socket_acceptor_service(boost::asio::io_service& io_service)
      : boost::asio::detail::service_base<socket_acceptor_service>(io_service) {
  }

  /**
   * @brief 析构函数，销毁socket_acceptor_service对象。
   */
  virtual ~socket_acceptor_service() {}

  /**
   * @brief 构造函数，初始化实现类型对象。
   * @param impl 实现类型对象
   */
  void construct(implementation_type& impl) {
    impl.p_multiplexer.reset();
    impl.p_acceptor.reset();
  }

  /**
   * @brief 析构函数，销毁实现类型对象。
   * @param impl 实现类型对象
   */
  void destroy(implementation_type& impl) {
    impl.p_multiplexer.reset();
    impl.p_acceptor.reset();
  }

  /**
   * @brief 移动构造函数，将一个实现类型对象移动到另一个实现类型对象。
   * @param impl 目标实现类型对象
   * @param other 源实现类型对象
   */
  void move_construct(implementation_type& impl, implementation_type& other) {
    impl = std::move(other);
  }

  /**
   * @brief 移动赋值函数，将一个实现类型对象移动到另一个实现类型对象。
   * @param impl 目标实现类型对象
   * @param other 源实现类型对象
   */
  void move_assign(implementation_type& impl, implementation_type& other) {
    impl = std::move(other);
  }

  /**
   * @brief 打开acceptor。
   * @param impl 实现类型对象
   * @param protocol 协议类型
   * @param ec 错误码
   * @return 错误码
   */
  boost::system::error_code open(implementation_type& impl,
                                 const protocol_type& protocol,
                                 boost::system::error_code& ec) {
    if (!impl.p_acceptor) {
      impl.p_acceptor = std::make_shared<acceptor_session_type>();
      ec.assign(::common::error::success,
                ::common::error::get_error_category());
    } else {
      ec.assign(::common::error::device_or_resource_busy,
                ::common::error::get_error_category());
    }
    return ec;
  }

  /**
   * @brief 判断acceptor是否打开。
   * @param impl 实现类型对象
   * @return 如果acceptor打开则返回true，否则返回false
   */
  bool is_open(const implementation_type& impl) const {
    return impl.p_acceptor != nullptr;
  }

  /**
   * @brief 获取本地端点。
   * @param impl 实现类型对象
   * @param ec 错误码
   * @return 本地端点
   */
  endpoint_type local_endpoint(const implementation_type& impl,
                               boost::system::error_code& ec) const {
    if (!is_open(impl)) {
      ec.assign(::common::error::broken_pipe,
                ::common::error::get_error_category());

      return endpoint_type();
    }

    ec.assign(::common::error::success, ::common::error::get_error_category());
    return endpoint_type(0, impl.p_acceptor->next_local_endpoint(ec));
  }

  /**
   * @brief 关闭acceptor。
   * @param impl 实现类型对象
   * @param ec 错误码
   * @return 错误码
   */
  boost::system::error_code close(implementation_type& impl,
                                  boost::system::error_code& ec) {
    if (!is_open(impl)) {
      ec.assign(::common::error::broken_pipe,
                ::common::error::get_error_category());
      return ec;
    }

    impl.p_acceptor->Close();
    impl.p_multiplexer->RemoveAcceptor();
    impl.p_acceptor.reset();
    impl.p_multiplexer.reset();

    return ec;
  }

  /**
   * @brief 获取本地句柄。
   * @param impl 实现类型对象
   * @return 本地句柄
   */
  native_type native(implementation_type& impl) { return impl; }

  /**
   * @brief 获取本地句柄。
   * @param impl 实现类型对象
   * @return 本地句柄
   */
  native_handle_type native_handle(implementation_type& impl) { return impl; }

  /**
   * @brief 设置socket选项。
   * @tparam SettableSocketOption 可设置的socket选项类型
   * @param impl 实现类型对象
   * @param option socket选项
   * @param ec 错误码
   * @return 错误码
   */
  template <typename SettableSocketOption>
  boost::system::error_code set_option(implementation_type& impl,
                                       const SettableSocketOption& option,
                                       boost::system::error_code& ec) {
    ec.assign(::common::error::function_not_supported,
              ::common::error::get_error_category());
    return ec;
  }

  /**
   * @brief 获取socket选项。
   * @tparam GettableSocketOption 可获取的socket选项类型
   * @param impl 实现类型对象
   * @param option socket选项
   * @param ec 错误码
   * @return 错误码
   */
  template <typename GettableSocketOption>
  boost::system::error_code get_option(const implementation_type& impl,
                                       GettableSocketOption& option,
                                       boost::system::error_code& ec) const {
    ec.assign(::common::error::function_not_supported,
              ::common::error::get_error_category());
    return ec;
  }

  /**
   * @brief 控制IO操作。
   * @tparam IoControlCommand IO控制命令类型
   * @param impl 实现类型对象
   * @param command IO控制命令
   * @param ec 错误码
   * @return 错误码
   */
  template <typename IoControlCommand>
  boost::system::error_code io_control(implementation_type& impl,
                                       IoControlCommand& command,
                                       boost::system::error_code& ec) {
    ec.assign(::common::error::function_not_supported,
              ::common::error::get_error_category());
    return ec;
  }

  /**
   * @brief 绑定端点。
   * @param impl 实现类型对象
   * @param endpoint 端点
   * @param ec 错误码
   * @return 错误码
   */
  boost::system::error_code bind(implementation_type& impl,
                                 const endpoint_type& endpoint,
                                 boost::system::error_code& ec) {
    if (impl.p_multiplexer) {
      ec.assign(::common::error::device_or_resource_busy,
                ::common::error::get_error_category());

      return ec;
    }

    impl.p_multiplexer = protocol_type::multiplexers_manager_.GetMultiplexer(
        this->get_io_service(), endpoint.next_layer_endpoint(), ec);
    if (ec) {
      return ec;
    }

    impl.p_multiplexer->SetAcceptor(ec, impl.p_acceptor);

    return ec;
  }

  /**
   * @brief 监听连接。
   * @param impl 实现类型对象
   * @param backlog 连接队列的最大长度
   * @param ec 错误码
   * @return 错误码
   */
  boost::system::error_code listen(implementation_type& impl, int backlog,
                                   boost::system::error_code& ec) {
    impl.p_acceptor->Listen(backlog, ec);
    return ec;
  }

  /**
   * @brief 接受连接。
   * @tparam Protocol1 协议类型
   * @tparam SocketService socket服务类型
   * @param impl 实现类型对象
   * @param peer peer socket对象
   * @param p_peer_endpoint peer端点指针
   * @param ec 错误码
   * @return 错误码
   */
  template <typename Protocol1, typename SocketService>
  boost::system::error_code accept(
      implementation_type& impl,
      boost::asio::basic_socket<Protocol1, SocketService>& peer,
      endpoint_type* p_peer_endpoint, boost::system::error_code& ec,
      typename std::enable_if<boost::thread_detail::is_convertible<
          protocol_type, Protocol1>::value>::type* = 0) {
    try {
      ec.clear();
      auto future_value =
          async_accept(impl, peer, p_peer_endpoint, boost::asio::use_future);
      future_value.get();
      ec.assign(::common::error::success,
                ::common::error::get_error_category());
    } catch (const std::system_error& e) {
      ec.assign(e.code().value(), ::common::error::get_error_category());
    }
    return ec;
  }

  /**
   * @brief 异步接受连接。
   * @tparam Protocol1 协议类型
   * @tparam SocketService socket服务类型
   * @tparam AcceptHandler 接受处理器类型
   * @param impl 实现类型对象
   * @param peer peer socket对象
   * @param p_peer_endpoint peer端点指针
   * @param handler 接受处理器
   * @return 异步结果
   */
  template <typename Protocol1, typename SocketService, typename AcceptHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(AcceptHandler, void(boost::system::error_code))
      async_accept(implementation_type& impl,
                   boost::asio::basic_socket<Protocol1, SocketService>& peer,
                   endpoint_type* p_peer_endpoint,
                   BOOST_ASIO_MOVE_ARG(AcceptHandler) handler,
                   typename std::enable_if<boost::thread_detail::is_convertible<
                       protocol_type, Protocol1>::value>::type* = 0) {
    boost::asio::detail::async_result_init<AcceptHandler,
                                           void(boost::system::error_code)>
        init(BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler));

    if (!is_open(impl)) {
      this->get_io_service().post(
          boost::asio::detail::binder1<decltype(init.handler),
                                       boost::system::error_code>(
              init.handler, boost::system::error_code(
                                ::common::error::broken_pipe,
                                ::common::error::get_error_category())));
      return init.result.get();
    }

    if (!impl.p_multiplexer) {
      this->get_io_service().post(
          boost::asio::detail::binder1<decltype(init.handler),
                                       boost::system::error_code>(
              init.handler, boost::system::error_code(
                                ::common::error::bad_address,
                                ::common::error::get_error_category())));
      return init.result.get();
    }

    using op =
        io::pending_accept_operation<decltype(init.handler), protocol_type>;
    typename op::ptr p = {
        boost::asio::detail::addressof(init.handler),
        boost_asio_handler_alloc_helpers::allocate(sizeof(op), init.handler),
        0};

    p.p = new (p.v) op(peer, nullptr, init.handler);

    impl.p_acceptor->PushAcceptOp(p.p);

    p.v = p.p = 0;

    return init.result.get();
  }

 private:
  /**
   * @brief 关闭服务。
   */
  void shutdown_service() {}
};

#include <boost/asio/detail/pop_options.hpp>

}  // connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_SOCKET_ACCEPTOR_SERVICE_H_
