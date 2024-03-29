#ifndef UDT_CONNECTED_PROTOCOL_RESOLVER_QUERY_H_
#define UDT_CONNECTED_PROTOCOL_RESOLVER_QUERY_H_

#include <cstdint>

namespace connected_protocol {

/**
 * @brief 解析器查询类
 * 
 * 用于表示解析器查询的类模板。该类模板用于封装下层协议的解析器查询，并提供了访问和操作解析器查询的方法。
 * 
 * @tparam Protocol 协议类型
 */
template <class Protocol>
class ResolverQuery {
 public:
  using protocol_type = Protocol;
  using SocketId = uint32_t;
  using NextLayer = typename Protocol::next_layer_protocol;
  using NextLayerQuery = typename NextLayer::resolver::query;

 public:
  /**
   * @brief 构造函数
   * 
   * 使用给定的下层协议查询和套接字ID构造一个解析器查询对象。
   * 
   * @param next_layer_query 下层协议查询对象
   * @param socket_id 套接字ID，默认为0
   */
  ResolverQuery(const NextLayerQuery& next_layer_query, SocketId socket_id = 0)
      : next_layer_query_(next_layer_query), socket_id_(socket_id) {}

  /**
   * @brief 拷贝构造函数
   * 
   * 使用给定的解析器查询对象构造一个新的解析器查询对象。
   * 
   * @param other 要拷贝的解析器查询对象
   */
  ResolverQuery(const ResolverQuery& other)
      : next_layer_query_(other.next_layer_query_),
        socket_id_(other.socket_id_) {}

  /**
   * @brief 获取下层协议查询对象
   * 
   * 返回封装的下层协议查询对象。
   * 
   * @return 下层协议查询对象
   */
  NextLayerQuery next_layer_query() const { return next_layer_query_; }

  /**
   * @brief 获取套接字ID
   * 
   * 返回解析器查询对象的套接字ID。
   * 
   * @return 套接字ID
   */
  SocketId socket_id() const { return socket_id_; }

 protected:
  NextLayerQuery next_layer_query_; /**< 下层协议查询对象 */
  SocketId socket_id_; /**< 套接字ID */
};

}  // connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_RESOLVER_QUERY_H_
