#ifndef UDT_CONNECTED_PROTOCOL_MULTIPLEXERS_MANAGER_H_
#define UDT_CONNECTED_PROTOCOL_MULTIPLEXERS_MANAGER_H_

#include <map>
#include <memory>

#include <boost/log/trivial.hpp>
#include <boost/thread/mutex.hpp>

#include "udt/connected_protocol/multiplexer.h"

namespace connected_protocol {

/**
 * @brief 多路复用器管理器类
 * 
 * 该类用于管理多路复用器对象，提供获取和清理多路复用器的功能。
 * 
 * @tparam Protocol 多路复用器使用的协议类型
 */
template <class Protocol>
class MultiplexerManager {
 public:
  using NextSocket = typename Protocol::next_layer_protocol::socket;
  using NextLayerEndpoint = typename Protocol::next_layer_protocol::endpoint;

 private:
  using MultiplexerPtr = typename Multiplexer<Protocol>::Ptr;
  using MultiplexersMap = std::map<NextLayerEndpoint, MultiplexerPtr>;

 public:
  /**
   * @brief 构造一个 MultiplexerManager 对象
   */
  MultiplexerManager() : mutex_(), multiplexers_() {}
  
  /**
   * @brief 获取多路复用器对象
   * 
   * 如果给定的本地端点对应的多路复用器不存在，则创建一个新的多路复用器对象，并将其添加到管理器中。
   * 如果创建多路复用器失败，则返回 nullptr。
   * 
   * @param io_service Boost.Asio 的 io_service 对象
   * @param next_local_endpoint 下一层协议的本地端点
   * @param ec 用于返回错误码的对象
   * @return MultiplexerPtr 多路复用器对象的智能指针
   */
  MultiplexerPtr GetMultiplexer(boost::asio::io_service &io_service,
                                   const NextLayerEndpoint &next_local_endpoint,
                                   boost::system::error_code &ec) {
    boost::mutex::scoped_lock lock(mutex_);
    auto multiplexer_it = multiplexers_.find(next_local_endpoint);
    if (multiplexer_it == multiplexers_.end()) {
      NextSocket next_layer_socket(io_service);
      next_layer_socket.open(next_local_endpoint.protocol());
      // 空的端点将会将套接字绑定到一个可用的端口
      next_layer_socket.bind(next_local_endpoint, ec);
      if (ec) {
        BOOST_LOG_TRIVIAL(error)
            << "Could not bind multiplexer on local endpoint";
        return nullptr;
      }

      MultiplexerPtr p_multiplexer =
          Multiplexer<Protocol>::Create(this, std::move(next_layer_socket));

      boost::system::error_code ec;

      multiplexers_[p_multiplexer->local_endpoint(ec)] = p_multiplexer;
      p_multiplexer->Start();

      return p_multiplexer;
    }

    return multiplexer_it->second;
  }

  /**
   * @brief 清理多路复用器对象
   * 
   * 根据给定的本地端点，从管理器中移除对应的多路复用器对象，并停止该多路复用器的运行。
   * 
   * @param next_local_endpoint 下一层协议的本地端点
   */
  void CleanMultiplexer(const NextLayerEndpoint &next_local_endpoint) {
    boost::mutex::scoped_lock lock(mutex_);
    if (multiplexers_.find(next_local_endpoint) != multiplexers_.end()) {
      boost::system::error_code ec;
      multiplexers_[next_local_endpoint]->Stop(ec);
      multiplexers_.erase(next_local_endpoint);
      // @todo should ec be swallowed here?
    }
  }

 private:
  boost::mutex mutex_;
  MultiplexersMap multiplexers_;
};

}  // connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_MULTIPLEXERS_MANAGER_H_
