#ifndef UDT_CONNECTED_PROTOCOL_RESOLVER_H_
#define UDT_CONNECTED_PROTOCOL_RESOLVER_H_

#include <cstdint>

#include <vector>

#include <boost/asio/io_service.hpp>

#include <boost/system/error_code.hpp>

#include "udt/common/error/error.h"

#include "udt/connected_protocol/resolver_query.h"

/**
 * @brief 连接协议的解析器类
 * 
 * 该类用于解析连接协议的查询，并返回相应的端点迭代器。
 * 
 * @tparam Protocol 连接协议类型
 */
namespace connected_protocol {

template <class Protocol>
class Resolver {
 private:
  /**
   * @brief 端点迭代器类
   * 
   * 该类用于迭代连接协议的端点集合。
   */
  class EndpointIterator {
   public:
    /**
     * @brief 默认构造函数
     * 
     * 创建一个空的端点迭代器。
     */
    EndpointIterator() : endpoints_(1), index_(0) {}

    /**
     * @brief 构造函数
     * 
     * 创建一个包含指定端点集合的端点迭代器。
     * 
     * @param endpoints 端点集合
     */
    EndpointIterator(std::vector<typename Protocol::endpoint> endpoints)
        : endpoints_(endpoints), index_(0) {}

    /**
     * @brief 解引用操作符
     * 
     * 返回当前迭代器指向的端点引用。
     * 
     * @return 当前迭代器指向的端点引用
     */
    typename Protocol::endpoint& operator*() { return endpoints_[index_]; }

    /**
     * @brief 成员访问操作符
     * 
     * 返回指向当前迭代器指向的端点的指针。
     * 
     * @return 指向当前迭代器指向的端点的指针
     */
    typename Protocol::endpoint* operator->() { return &endpoints_[index_]; }

    /**
     * @brief 前置递增操作符
     * 
     * 将迭代器向前移动一位，并返回移动后的端点引用。
     * 
     * @return 移动后的端点引用
     */
    typename Protocol::endpoint& operator++() {
      ++index_;
      return endpoints_[index_];
    }

    /**
     * @brief 后置递增操作符
     * 
     * 将迭代器向前移动一位，并返回移动前的端点引用。
     * 
     * @return 移动前的端点引用
     */
    typename Protocol::endpoint operator++(int) {
      ++index_;
      return endpoints_[index_ - 1];
    }

    /**
     * @brief 前置递减操作符
     * 
     * 将迭代器向后移动一位，并返回移动后的端点引用。
     * 
     * @return 移动后的端点引用
     */
    typename Protocol::endpoint& operator--() {
      --index_;
      return endpoints_[index_];
    }

    /**
     * @brief 后置递减操作符
     * 
     * 将迭代器向后移动一位，并返回移动前的端点引用。
     * 
     * @return 移动前的端点引用
     */
    typename Protocol::endpoint operator--(int) {
      --index_;
      return endpoints_[index_ + 1];
    }

   private:
    std::vector<typename Protocol::endpoint> endpoints_; /**< 端点集合 */
    std::size_t index_; /**< 当前索引 */
  };

  using NextLayer = typename Protocol::next_layer_protocol;
  using NextLayerEndpoint = typename NextLayer::endpoint;

 public:
  using protocol_type = Protocol; /**< 协议类型 */
  using endpoint_type = typename Protocol::endpoint; /**< 端点类型 */
  using query = ResolverQuery<Protocol>; /**< 查询类型 */
  using iterator = EndpointIterator; /**< 迭代器类型 */

 public:
  /**
   * @brief 构造函数
   * 
   * 创建一个连接协议解析器对象。
   * 
   * @param io_service IO服务对象
   */
  Resolver(boost::asio::io_service& io_service) : io_service_(io_service) {}

  /**
   * @brief 解析查询
   * 
   * 根据指定的查询解析连接协议，并返回相应的端点迭代器。
   * 
   * @param q 查询对象
   * @param ec 错误码对象
   * @return 端点迭代器
   */
  iterator resolve(const query& q, boost::system::error_code& ec) {
    typename NextLayer::resolver next_layer_resolver(io_service_);
    auto next_layer_iterator =
        next_layer_resolver.resolve(q.next_layer_query(), ec);
    if (ec) {
      return iterator();
    }

    std::vector<endpoint_type> result;
    result.emplace_back(q.socket_id(), NextLayerEndpoint(*next_layer_iterator));
    ec.assign(::common::error::success, ::common::error::get_error_category());

    return iterator(result);
  }
 private:
  boost::asio::io_service& io_service_; /**< IO服务对象 */
};

}  // connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_RESOLVER_H_
