#ifndef UDT_SRC_FILE_TRANSPORT_CLIENT_GOODBYESENDER_H_
#define UDT_SRC_FILE_TRANSPORT_CLIENT_GOODBYESENDER_H_


#include "udt/ip/udt.h"


class GoodbyeSender {
  using udt_protocol = ip::udt<>;
  using Buffer = std::array<u_char, 100000>;
  using SendHandler =
	  std::function<void(const boost::system::error_code &, std::size_t)>;

 public:
  GoodbyeSender(udt_protocol::socket& socket, Buffer& buffer, SendHandler& send_handler)
	  : socket_(socket), buffer_(buffer), send_handler_(send_handler) {
	std::cout << "GoodbyeSender constructor" << std::endl;
  }

  ~GoodbyeSender() {
	std::cout << "GoodbyeSender destructor" << std::endl;
	std::string goodbye = "client " + std::to_string(getpid()) + " goodbye";
	std::copy(goodbye.begin(), goodbye.end(), buffer_.begin());
	std::fill(buffer_.begin() + goodbye.size(), buffer_.end(), 0);
	socket_.async_send(boost::asio::buffer(buffer_.data(), goodbye.length()), send_handler_);
  }


 private:
  udt_protocol::socket& socket_;
  Buffer& buffer_;
  SendHandler& send_handler_;
};


#endif //UDT_SRC_FILE_TRANSPORT_CLIENT_GOODBYESENDER_H_
