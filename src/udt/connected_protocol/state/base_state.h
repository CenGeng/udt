#ifndef UDT_CONNECTED_PROTOCOL_STATE_BASE_STATE_H_
#define UDT_CONNECTED_PROTOCOL_STATE_BASE_STATE_H_

#include <boost/chrono.hpp>
#include <memory>

#include "udt/common/error/error.h"
#include "udt/connected_protocol/io/read_op.h"
#include "udt/connected_protocol/io/write_op.h"
#include "udt/connected_protocol/logger/log_entry.h"

namespace connected_protocol {
namespace state {

/**
 * @brief 基础状态类
 *
 * @tparam Protocol 协议类型
 */
template <class Protocol>
class BaseState {
 public:
  using Ptr = std::shared_ptr<BaseState>;
  using ConnectionDatagram = typename Protocol::ConnectionDatagram;
  using ConnectionDatagramPtr = std::shared_ptr<ConnectionDatagram>;
  using ControlDatagram = typename Protocol::GenericControlDatagram;
  using SendDatagram = typename Protocol::SendDatagram;
  using DataDatagram = typename Protocol::DataDatagram;
  using Clock = typename Protocol::clock;
  using TimePoint = typename Protocol::time_point;
  using Timer = typename Protocol::timer;

 public:
  /**
   * @brief 状态类型枚举
   */
  enum type { CLOSED, CONNECTING, ACCEPTING, CONNECTED, TIMEOUT };

 public:
  /**
   * @brief 获取状态类型
   *
   * @return type 状态类型
   */
  virtual type GetType() = 0;

  /**
   * @brief 获取IO服务
   *
   * @return boost::asio::io_service& IO服务引用
   */
  boost::asio::io_service& get_io_service() { return io_service_; }

  /**
   * @brief 初始化状态
   */
  virtual void Init() {}

  /**
   * @brief 析构函数
   */
  virtual ~BaseState() {}

  /**
   * @brief 停止状态
   */
  virtual void Stop() {}

  /**
   * @brief 关闭状态
   */
  virtual void Close() {}

  /**
   * @brief 推入读操作
   *
   * @param read_op 读操作指针
   */
  virtual void PushReadOp(
      io::basic_pending_stream_read_operation<Protocol>* read_op) {
    // Drop op
    auto do_complete = [read_op]() {
      read_op->complete(
          boost::system::error_code(::common::error::not_connected,
                                    ::common::error::get_error_category()),
          0);
    };
    io_service_.post(do_complete);
  }

  /**
   * @brief 推入写操作
   *
   * @param write_op 写操作指针
   */
  virtual void PushWriteOp(io::basic_pending_write_operation* write_op) {
    // Drop op
    auto do_complete = [write_op]() {
      write_op->complete(
          boost::system::error_code(::common::error::not_connected,
                                    ::common::error::get_error_category()),
          0);
    };
    io_service_.post(do_complete);
  }

  /**
   * @brief 是否有待发送的数据包
   *
   * @return bool 是否有待发送的数据包
   */
  virtual bool HasPacketToSend() { return false; }

  /**
   * @brief 获取下一个计划发送的数据包
   *
   * @return SendDatagram* 下一个计划发送的数据包指针
   */
  virtual SendDatagram* NextScheduledPacket() { return nullptr; }

  /**
   * @brief 处理连接数据报
   *
   * @param p_connection_dgr 连接数据报指针
   */
  virtual void OnConnectionDgr(ConnectionDatagramPtr p_connection_dgr) {
    // Drop dgr
  }

  /**
   * @brief 处理控制数据报
   *
   * @param p_control_dgr 控制数据报指针
   */
  virtual void OnControlDgr(ControlDatagram* p_control_dgr) {
    // Drop dgr
  }

  /**
   * @brief 处理数据数据报
   *
   * @param p_datagram 数据数据报指针
   */
  virtual void OnDataDgr(DataDatagram* p_datagram) {
    // Drop dgr
  }

  /**
   * @brief 记录日志
   *
   * @param p_log 日志条目指针
   */
  virtual void Log(connected_protocol::logger::LogEntry* p_log) {}

  /**
   * @brief 重置日志
   */
  virtual void ResetLog() {}

  /**
   * @brief 获取数据包到达速度
   *
   * @return double 数据包到达速度
   */
  virtual double PacketArrivalSpeed() { return 0.0; }

  /**
   * @brief 获取估计的链路容量
   *
   * @return double 估计的链路容量
   */
  virtual double EstimatedLinkCapacity() { return 0.0; }

  /**
   * @brief 获取下一个计划发送数据包的时间
   *
   * @return boost::chrono::nanoseconds 下一个计划发送数据包的时间
   */
  virtual boost::chrono::nanoseconds NextScheduledPacketTime() {
    return boost::chrono::nanoseconds(0);
  }

 protected:
  /**
   * @brief 构造函数
   *
   * @param io_service IO服务引用
   */
  BaseState(boost::asio::io_service& io_service) : io_service_(io_service) {}

 private:
  boost::asio::io_service& io_service_;
};

}  // namespace state
}  // namespace connected_protocol

#endif  // UDT_CONNECTED_PROTOCOL_STATE_BASE_STATE_H_
