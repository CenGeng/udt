#ifndef UDT_CONNECTED_PROTOCOL_ENDPOINT_H_
#define UDT_CONNECTED_PROTOCOL_ENDPOINT_H_

#include <cstdint>

namespace connected_protocol {

/**
 * @brief 表示连接协议的端点类。
 *
 * @tparam TProtocol 连接协议类型。
 */
template <class TProtocol>
class Endpoint {
 public:
  using protocol_type = TProtocol;
  using data_type = boost::asio::detail::socket_addr_type;
  using NextLayer = typename TProtocol::next_layer_protocol;
  using NextLayerEndpoint = typename NextLayer::endpoint;
  using SocketId = uint32_t;

 public:
  /**
   * @brief 默认构造函数。
   */
  Endpoint() : socket_id_(0), next_layer_endpoint_() {}

  /**
   * @brief 构造函数。
   *
   * @param socket_id 套接字ID。
   * @param next_layer_endpoint 下层协议的端点。
   */
  Endpoint(SocketId socket_id, const NextLayerEndpoint& next_layer_endpoint)
      : socket_id_(socket_id), next_layer_endpoint_(next_layer_endpoint) {}

  /**
   * @brief 拷贝构造函数。
   *
   * @param other 要拷贝的对象。
   */
  Endpoint(const Endpoint& other)
      : socket_id_(other.socket_id_),
        next_layer_endpoint_(other.next_layer_endpoint_) {}

  /**
   * @brief 移动构造函数。
   *
   * @param other 要移动的对象。
   */
  Endpoint(Endpoint&& other)
      : socket_id_(std::move(other.socket_id_)),
        next_layer_endpoint_(std::move(other.next_layer_endpoint_)) {}

  /**
   * @brief 拷贝赋值运算符。
   *
   * @param other 要拷贝的对象。
   * @return 当前对象的引用。
   */
  Endpoint& operator=(const Endpoint& other) {
    socket_id_ = other.socket_id_;
    next_layer_endpoint_ = other.next_layer_endpoint_;

    return *this;
  }

  /**
   * @brief 移动赋值运算符。
   *
   * @param other 要移动的对象。
   * @return 当前对象的引用。
   */
  Endpoint& operator=(Endpoint&& other) {
    socket_id_ = std::move(other.socket_id_);
    next_layer_endpoint_ = std::move(other.next_layer_endpoint_);

    return *this;
  }

  /**
   * @brief 获取连接协议类型。
   *
   * @return 连接协议类型。
   */
  protocol_type protocol() const { return protocol_type(); }

  /**
   * @brief 获取套接字ID。
   *
   * @return 套接字ID。
   */
  SocketId socket_id() const { return socket_id_; }

  /**
   * @brief 设置套接字ID。
   *
   * @param socket_id 套接字ID。
   */
  void socket_id(SocketId socket_id) { socket_id_ = socket_id; }

  /**
   * @brief 获取下层协议的端点。
   *
   * @return 下层协议的端点。
   */
  NextLayerEndpoint next_layer_endpoint() const { return next_layer_endpoint_; }

  /**
   * @brief 设置下层协议的端点。
   *
   * @param next_layer_endpoint 下层协议的端点。
   */
  void next_layer_endpoint(const NextLayerEndpoint& next_layer_endpoint) {
    next_layer_endpoint_ = next_layer_endpoint;
  }

  /**
   * @brief 判断两个端点是否相等。
   *
   * @param rhs 另一个端点。
   * @return 如果两个端点相等，则返回true；否则返回false。
   */
  bool operator==(const Endpoint& rhs) const {
    return socket_id_ == rhs.socket_id_ &&
           next_layer_endpoint_ == rhs.next_layer_endpoint_;
  }

  /**
   * @brief 比较两个端点的大小。
   *
   * @param rhs 另一个端点。
   * @return 如果当前端点小于另一个端点，则返回true；否则返回false。
   */
  bool operator<(const Endpoint& rhs) const {
    if (socket_id_ != rhs.socket_id_) {
      return socket_id_ < rhs.socket_id_;
    }

    return next_layer_endpoint_ < rhs.next_layer_endpoint_;
  }

  /**
   * @brief 获取底层端点的原生类型指针。
   *
   * @return 底层端点的原生类型指针。
   */
  data_type* data() { return next_layer_endpoint_.data(); }

  /**
   * @brief 获取底层端点的原生类型指针。
   *
   * @return 底层端点的原生类型指针。
   */
  const data_type* data() const { return next_layer_endpoint_.data(); }

  /**
   * @brief 获取底层端点的大小。
   *
   * @return 底层端点的大小。
   */
  std::size_t size() const { return next_layer_endpoint_.size(); }

  /**
   * @brief 设置底层端点的大小。
   *
   * @param new_size 新的大小。
   */
  void resize(std::size_t new_size) { next_layer_endpoint_.resize(new_size); }

  /**
   * @brief 获取底层端点的容量。
   *
   * @return 底层端点的容量。
   */
  std::size_t capacity() const { return next_layer_endpoint_.capacity(); }

 private:
  SocketId socket_id_;                     ///< 套接字ID。
  NextLayerEndpoint next_layer_endpoint_;  ///< 下层协议的端点。
};

}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_ENDPOINT_H_
