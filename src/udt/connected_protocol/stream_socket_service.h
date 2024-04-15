#ifndef UDT_CONNECTED_PROTOCOL_STREAM_SOCKET_SERVICE_H_
#define UDT_CONNECTED_PROTOCOL_STREAM_SOCKET_SERVICE_H_

#include <boost/asio/io_service.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/use_future.hpp>
#include <memory>

#include "udt/common/error/error.h"
#include "udt/connected_protocol/io/connect_op.h"
#include "udt/connected_protocol/io/read_op.h"
#include "udt/connected_protocol/io/write_op.h"
#include "udt/connected_protocol/state/connecting_state.h"

namespace connected_protocol {

/**
 * @brief 用于处理流式套接字的服务类。
 *
 * stream_socket_service 是一个模板类，用于处理特定协议的流式套接字操作。
 * 它继承自 boost::asio::detail::service_base，是一个服务类的基类。
 *
 * @tparam Protocol 协议类型，用于指定流式套接字的协议。
 */
template <class Protocol>
class stream_socket_service : public boost::asio::detail::service_base<
                                  stream_socket_service<Protocol>> {
 public:
  using protocol_type = Protocol;  ///< 协议类型

  struct implementation_type {
    implementation_type()
        : p_multiplexer(nullptr), p_session(nullptr), timeout(60) {}

    std::shared_ptr<typename protocol_type::multiplexer>
        p_multiplexer;  ///< 多路复用器指针
    std::shared_ptr<typename protocol_type::socket_session>
        p_session;  ///< 套接字会话指针
    int timeout;    ///< 超时时间
  };

  using endpoint_type = typename protocol_type::endpoint;  ///< 端点类型

  using native_handle_type = implementation_type&;  ///< 本地句柄类型
  using native_type = native_handle_type;           ///< 本地类型

 private:
  using next_endpoint_type = typename protocol_type::next_layer_protocol::
      endpoint;  ///< 下一层协议的端点类型
  using multiplexer = typename protocol_type::multiplexer;  ///< 多路复用器类型

  using ConnectingState = typename connected_protocol::state::ConnectingState<
      protocol_type>;  ///< 连接状态类型
  using ClosedState = typename connected_protocol::state::ClosedState<
      protocol_type>;  ///< 关闭状态类型

 public:
  /**
   * @brief 构造函数。
   *
   * @param io_service IO 服务对象的引用。
   */
  explicit stream_socket_service(boost::asio::io_service& io_service);

  /**
   * @brief 析构函数。
   */
  virtual ~stream_socket_service();

  /**
   * @brief 构造套接字实现。
   *
   * @param impl 套接字实现对象的引用。
   */
  void construct(implementation_type& impl);

  /**
   * @brief 销毁套接字实现。
   *
   * @param impl 套接字实现对象的引用。
   */
  void destroy(implementation_type& impl);

  /**
   * @brief 移动构造套接字实现。
   *
   * @param impl 套接字实现对象的引用。
   * @param other 要移动的套接字实现对象的引用。
   */
  void move_construct(implementation_type& impl, implementation_type& other);

  /**
   * @brief 移动赋值套接字实现。
   *
   * @param impl 套接字实现对象的引用。
   * @param other 要移动赋值的套接字实现对象的引用。
   */
  void move_assign(implementation_type& impl, implementation_type& other);

  /**
   * @brief 打开套接字。
   *
   * @param impl 套接字实现对象的引用。
   * @param protocol 协议对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return boost::system::error_code 打开套接字的错误码。
   */
  boost::system::error_code open(implementation_type& impl,
                                 const protocol_type& protocol,
                                 boost::system::error_code& ec);

  /**
   * @brief 检查套接字是否打开。
   *
   * @param impl 套接字实现对象的引用。
   * @return bool 如果套接字打开则返回 true，否则返回 false。
   */
  bool is_open(const implementation_type& impl) const;

  /**
   * @brief 获取远程端点。
   *
   * @param impl 套接字实现对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return endpoint_type 远程端点。
   */
  endpoint_type remote_endpoint(const implementation_type& impl,
                                boost::system::error_code& ec) const;

  /**
   * @brief 获取本地端点。
   *
   * @param impl 套接字实现对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return endpoint_type 本地端点。
   */
  endpoint_type local_endpoint(const implementation_type& impl,
                               boost::system::error_code& ec) const;

  /**
   * @brief 关闭套接字。
   *
   * @param impl 套接字实现对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return boost::system::error_code 关闭套接字的错误码。
   */
  boost::system::error_code close(implementation_type& impl,
                                  boost::system::error_code& ec);

  /**
   * @brief 获取本地句柄。
   *
   * @param impl 套接字实现对象的引用。
   * @return native_type 本地句柄。
   */
  native_type native(implementation_type& impl);

  /**
   * @brief 获取本地句柄。
   *
   * @param impl 套接字实现对象的引用。
   * @return native_handle_type 本地句柄。
   */
  native_handle_type native_handle(implementation_type& impl);

  /**
   * @brief 检查是否在标记位置。
   *
   * @param impl 套接字实现对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return bool 如果在标记位置则返回 true，否则返回 false。
   */
  bool at_mark(const implementation_type& impl,
               boost::system::error_code& ec) const;

  /**
   * @brief 获取可用字节数。
   *
   * @param impl 套接字实现对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return std::size_t 可用字节数。
   */
  std::size_t available(const implementation_type& impl,
                        boost::system::error_code& ec) const;

  /**
   * @brief 取消操作。
   *
   * @param impl 套接字实现对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return boost::system::error_code 取消操作的错误码。
   */
  boost::system::error_code cancel(implementation_type& impl,
                                   boost::system::error_code& ec);

  /**
   * @brief 绑定套接字。
   *
   * @param impl 套接字实现对象的引用。
   * @param local_endpoint 本地端点对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return boost::system::error_code 绑定套接字的错误码。
   */
  boost::system::error_code bind(implementation_type& impl,
                                 const endpoint_type& local_endpoint,
                                 boost::system::error_code& ec);

  /**
   * @brief 连接套接字。
   *
   * @param impl 套接字实现对象的引用。
   * @param peer_endpoint 远程端点对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return boost::system::error_code 连接套接字的错误码。
   */
  boost::system::error_code connect(implementation_type& impl,
                                    const endpoint_type& peer_endpoint,
                                    boost::system::error_code& ec);

  /**
   * @brief 异步连接套接字。
   *
   * @tparam ConnectHandler 连接处理器类型。
   * @param impl 套接字实现对象的引用。
   * @param peer_endpoint 远程端点对象的引用。
   * @param handler 连接处理器。
   * @return BOOST_ASIO_INITFN_RESULT_TYPE(ConnectHandler,
   * void(boost::system::error_code)) 异步操作的结果类型。
   */
  template <typename ConnectHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(ConnectHandler, void(boost::system::error_code))
  async_connect(implementation_type& impl, const endpoint_type& peer_endpoint,
                BOOST_ASIO_MOVE_ARG(ConnectHandler) handler);

  /**
   * @brief 设置套接字选项。
   *
   * @tparam SettableSocketOption 可设置的套接字选项类型。
   * @param impl 套接字实现对象的引用。
   * @param option 套接字选项对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return boost::system::error_code 设置套接字选项的错误码。
   */
  template <typename SettableSocketOption>
  boost::system::error_code set_option(implementation_type& impl,
                                       const SettableSocketOption& option,
                                       boost::system::error_code& ec);

  /**
   * @brief 获取套接字选项。
   *
   * @tparam GettableSocketOption 可获取的套接字选项类型。
   * @param impl 套接字实现对象的引用。
   * @param option 套接字选项对象的引用。
   * @param ec 用于返回错误码的引用。
   * @return boost::system::error_code 获取套接字选项的错误码。
   */
  template <typename GettableSocketOption>
  boost::system::error_code get_option(const implementation_type& impl,
                                       GettableSocketOption& option,
                                       boost::system::error_code& ec) const;

  /**
   * @brief 发送数据。
   *
   * @tparam ConstBufferSequence 常量缓冲区序列类型。
   * @param impl 套接字实现对象的引用。
   * @param buffers 缓冲区序列对象的引用。
   * @param flags 消息标志。
   * @param ec 用于返回错误码的引用。
   * @return std::size_t 发送的字节数。
   */
  template <typename ConstBufferSequence>
  std::size_t send(implementation_type& impl,
                   const ConstBufferSequence& buffers,
                   boost::asio::socket_base::message_flags flags,
                   boost::system::error_code& ec);

  /**
   * @brief 异步发送数据。
   *
   * @tparam ConstBufferSequence 常量缓冲区序列类型。
   * @tparam WriteHandler 写处理器类型。
   * @param impl 套接字实现对象的引用。
   * @param buffers 缓冲区序列对象的引用。
   * @param handler 写处理器。
   * @return BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler,
   * void(boost::system::error_code, std::size_t)) 异步操作的结果类型。
   */
  template <typename ConstBufferSequence, typename WriteHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler,
                                void(boost::system::error_code, std::size_t))
  async_send(implementation_type& impl, const ConstBufferSequence& buffers,
             boost::asio::socket_base::message_flags flags,
             BOOST_ASIO_MOVE_ARG(WriteHandler) handler) {
    boost::asio::detail::async_result_init<
        WriteHandler, void(boost::system::error_code, std::size_t)>
        init(std::forward<WriteHandler>(handler));

    if (!impl.p_session) {
      this->get_io_service().post(
          boost::asio::detail::binder2<decltype(init.handler),
                                       boost::system::error_code, std::size_t>(
              init.handler,
              boost::system::error_code(::common::error::not_connected,
                                        ::common::error::get_error_category()),
              0));
      return init.result.get();
    }

    if (boost::asio::buffer_size(buffers) == 0) {
      this->get_io_service().post(
          boost::asio::detail::binder2<decltype(init.handler),
                                       boost::system::error_code, std::size_t>(
              init.handler,
              boost::system::error_code(::common::error::success,
                                        ::common::error::get_error_category()),
              0));
      return init.result.get();
    }

    using write_op_type = io::pending_write_operation<ConstBufferSequence,
                                                      decltype(init.handler)>;
    typename write_op_type::ptr p = {
        boost::asio::detail::addressof(init.handler),
        boost_asio_handler_alloc_helpers::allocate(sizeof(write_op_type),
                                                   init.handler),
        0};

    p.p = new (p.v) write_op_type(buffers, std::move(init.handler));

    impl.p_session->PushWriteOp(p.p);

    p.v = p.p = 0;

    return init.result.get();
  }

  /**
   * @brief 从连接的套接字接收数据。
   *
   * @tparam MutableBufferSequence 可变缓冲区序列的类型
   * @param impl 套接字的实现类型
   * @param buffers 接收数据的可变缓冲区序列
   * @param flags 消息标志
   * @param ec 用于存储错误码的对象
   * @return 成功接收的字节数，如果发生错误则返回0
   */
  template <typename MutableBufferSequence>
  std::size_t receive(implementation_type& impl,
                      const MutableBufferSequence& buffers,
                      boost::asio::socket_base::message_flags flags,
                      boost::system::error_code& ec) {
    try {
      ec.clear();
      auto future_value =
          async_receive(impl, buffers, flags, boost::asio::use_future);
      return future_value.get();
    } catch (const std::system_error& e) {
      ec.assign(e.code().value(), ::common::error::get_error_category());
      return 0;
    }
  }

  /**
   * 异步接收数据。
   *
   * 此函数用于异步接收数据到指定的缓冲区序列中。接收操作完成后，将调用指定的处理程序。
   *
   * @param impl 实现类型的引用，表示要进行接收操作的套接字。
   * @param buffers 可变缓冲区序列，表示接收到的数据将被写入其中。
   * @param flags 套接字标志，用于指定接收操作的行为。
   * @param handler 完成处理程序，将在接收操作完成时被调用。
   *
   * @return
   * 如果操作立即完成，则返回接收到的字节数；否则，返回一个异步结果对象，可以用于获取操作的结果。
   */
  template <typename MutableBufferSequence, typename ReadHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler,
                                void(boost::system::error_code, std::size_t))
  async_receive(implementation_type& impl, const MutableBufferSequence& buffers,
                boost::asio::socket_base::message_flags flags,
                BOOST_ASIO_MOVE_ARG(ReadHandler) handler) {
    boost::asio::detail::async_result_init<
        ReadHandler, void(boost::system::error_code, std::size_t)>
        init(std::forward<ReadHandler>(handler));

    if (!impl.p_session) {
      this->get_io_service().post(
          boost::asio::detail::binder2<decltype(init.handler),
                                       boost::system::error_code, std::size_t>(
              init.handler,
              boost::system::error_code(::common::error::not_connected,
                                        ::common::error::get_error_category()),
              0));
      return init.result.get();
    }

    if (boost::asio::buffer_size(buffers) == 0) {
      this->get_io_service().post(
          boost::asio::detail::binder2<decltype(init.handler),
                                       boost::system::error_code, std::size_t>(
              init.handler,
              boost::system::error_code(::common::error::success,
                                        ::common::error::get_error_category()),
              0));
      return init.result.get();
    }

    using read_op_type = io::pending_stream_read_operation<
        MutableBufferSequence, decltype(init.handler), protocol_type>;
    typename read_op_type::ptr p = {
        boost::asio::detail::addressof(init.handler),
        boost_asio_handler_alloc_helpers::allocate(sizeof(read_op_type),
                                                   init.handler),
        0};

    p.p = new (p.v) read_op_type(buffers, std::move(init.handler));

    impl.p_session->PushReadOp(p.p);

    p.v = p.p = 0;

    return init.result.get();
  }

  /**
   * @brief 关闭套接字连接。
   * 
   * 此函数用于关闭套接字连接。
   * 
   * @param impl 套接字实现类型。
   * @param what 关闭类型。
   * @param ec 错误码。
   * @return 错误码。
   */
  boost::system::error_code shutdown(
      implementation_type& impl, boost::asio::socket_base::shutdown_type what,
      boost::system::error_code& ec) {
    ec.assign(::common::error::success, ::common::error::get_error_category());
    return ec;
  }

 private:
  void shutdown_service() {}
};

#include <boost/asio/detail/pop_options.hpp>

}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_STREAM_SOCKET_SERVICE_H_
