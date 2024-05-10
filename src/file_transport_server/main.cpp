#include <boost/asio/io_service.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/system/error_code.hpp>
#include <boost/thread.hpp>

#include "udt/connected_protocol/logger/file_log.h"
#include "udt/ip/udt.h"

boost::mutex rmtx; // Global mutex
boost::mutex wmtx; // Global mutex


int main(int argc, char *argv[]) {
  if (argc!=2) {
	BOOST_LOG_TRIVIAL(error) << "Command help : ./udt_server [port]";
	return 1;
  }

  using udt_protocol = ip::udt<>;
  using Buffer = std::array<u_char, 150000>;

  using SocketPtr = std::shared_ptr<udt_protocol::socket>;
  using ReceiveHandler = std::function<void(const boost::system::error_code &,
											std::size_t, SocketPtr)>;
  using AcceptHandler =
	  std::function<void(const boost::system::error_code &, SocketPtr)>;
  using SendHandler =
	  std::function<void(const boost::system::error_code &, std::size_t)>;

  boost::asio::io_service io_service;
  boost::system::error_code resolve_ec;

  std::map<std::string, SocketPtr> client_map;	// for storing the pid and the corresponding socket


  Buffer in_buffer2;
  Buffer out_buffer2;

  SocketPtr p_socket(std::make_shared<udt_protocol::socket>(io_service));
  udt_protocol::acceptor acceptor(io_service);
  udt_protocol::resolver resolver(io_service);

  udt_protocol::resolver::query acceptor_udt_query(boost::asio::ip::udp::v4(),
												   argv[1]);

  auto acceptor_endpoint_it = resolver.resolve(acceptor_udt_query, resolve_ec);

  if (resolve_ec) {
	BOOST_LOG_TRIVIAL(error) << "Wrong argument provided" << std::endl;
	return 1;
  }

  udt_protocol::endpoint acceptor_endpoint(*acceptor_endpoint_it);

  AcceptHandler accept_handler;
  ReceiveHandler receive_handler;
  SendHandler send_handler;

  accept_handler = [&](const boost::system::error_code &ec,
					   SocketPtr p_socket) {
	if (ec) {
	  BOOST_LOG_TRIVIAL(trace)
		<< "Error on accept : " << ec.value() << " " << ec.message();
	  return;
	}

	BOOST_LOG_TRIVIAL(trace) << "Accepted";

	p_socket->async_send(boost::asio::buffer("Hello from server"), send_handler);

	// Clear the buffer
	std::fill(in_buffer2.begin(), in_buffer2.end(), 0);

	// Start an asynchronous read operation.
	p_socket->async_receive(boost::asio::buffer(in_buffer2),
							boost::bind(receive_handler, _1, _2, p_socket));

	// Accept next connection
	auto p_new_socket = std::make_shared<udt_protocol::socket>(io_service);
	acceptor.async_accept(*p_new_socket,
						  boost::bind(accept_handler, _1, p_new_socket));
  };

  receive_handler = [&](const boost::system::error_code &ec, std::size_t length,
						SocketPtr p_socket) {
	if (ec) {
	  BOOST_LOG_TRIVIAL(trace)
		<< "Error on receive : " << ec.value() << " " << ec.message();
	  return;
	}

	boost::lock_guard<boost::mutex> lock(rmtx); // Lock the mutex

	std::string str(in_buffer2.begin(), in_buffer2.begin() + length);
	std::fill(in_buffer2.begin(), in_buffer2.end(), 0);

	// Determine if the received message is "bey".
	if (str[0]=='b' && str[1]=='e' && str[2]=='y') {
	  std::string pid;
	  for (int i = 3; str[i]!='\0'; i++) {
		pid += str[i];
	  }
	  std::cout << pid << std::endl;

	  BOOST_LOG_TRIVIAL(trace) << "Client " << pid << " disconnected" << std::endl;
	  std::cout << "Please input the message you want to send to the clients: " << std::endl;

	  // Close the socket
	  p_socket->close();

	  // Delete the socket in client_map using pid as the key.
	  client_map.erase(pid);

	  // If client_map is empty, pause the program and wait for a new client to connect.
	  if (client_map.empty()){
		std::cout << "No client connected, please wait for the new client to connect!" << std::endl;
		while (true){
		  if (!client_map.empty()){
			return;
		  }
		}
	  }
	}

	// Determine if the received message is "client [pid] connected".
	std::string prefix = "client ";
	std::string suffix = " connected";

	if (str.rfind(prefix, 0)==0 && str.find(suffix, str.size() - suffix.size())!=std::string::npos) {
	  std::string client_pid_str = str.substr(prefix.size(), str.size() - prefix.size() - suffix.size());

	  BOOST_LOG_TRIVIAL(trace) << "Client " << client_pid_str << " connected" << std::endl;
	  std::cout << "Please input the message you want to send to the clients: " << std::endl;

	  // relationship between pid and socket
	  client_map[client_pid_str] = p_socket;

	  // Continue reading.
	  p_socket->async_receive(boost::asio::buffer(in_buffer2),
							  boost::bind(receive_handler, _1, _2, p_socket));
	  return;
	}



	// 遍历打印client_map
//	for (auto it = client_map.begin(); it!=client_map.end(); ++it) {
//	  std::cout << "Connected clients:" << std::endl;
//	  std::cout << it->first << " => " << it->second << std::endl;
//	}

	// Print the received data.
//	BOOST_LOG_TRIVIAL(trace) << "Received " << length << " bytes";
	std::cout << "Received: " << str << std::endl;
	std::cout << "Please input the message you want to send to the clients: " << std::endl;


	// Continue reading.
	p_socket->async_receive(boost::asio::buffer(in_buffer2),
							boost::bind(receive_handler, _1, _2, p_socket));
  };

  send_handler = [&](const boost::system::error_code &ec, std::size_t length) {
	if (ec) {
	  BOOST_LOG_TRIVIAL(trace)
		<< "Error on sent : " << ec.value() << " " << ec.message();
	  return;
	}

	boost::lock_guard<boost::mutex> lock(wmtx); // Lock the mutex

	std::cout << "Sent " << length << " bytes" << std::endl;

	// Get user input save it to the in_buffer1
	std::string line;
	std::cout << "Please input the message you want to send to the clients: " << std::endl;
	std::getline(std::cin, line);

	// Ensure that the line is not larger than the buffer
	if (line.size() > out_buffer2.size()) {
	  line.resize(out_buffer2.size());
	}
	while (1) {
	  if (line.empty()) {
		std::cout << "Can NOT make the input NULL!" << std::endl
				  << "Please input again: ";
		std::getline(std::cin, line);
	  } else
		break;
	}
	// add the pid to the line
	line = "Server: " + line;

	// Copy the line into the buffer
	std::copy(line.begin(), line.end(), out_buffer2.begin());

	// Fill the rest of the buffer with zeros
	std::fill(out_buffer2.begin() + line.size(), out_buffer2.end(), 0);


	  for (auto it = client_map.begin(); it!=client_map.end(); ++it) {
		it->second->async_send(boost::asio::buffer(out_buffer2, line.length()),
							   boost::bind(send_handler, _1, _2));
	  }


  };

  boost::system::error_code ec;

  acceptor.open();
  acceptor.bind(acceptor_endpoint, ec);
  acceptor.listen(100, ec);

  BOOST_LOG_TRIVIAL(trace) << "Accepting...";

  acceptor.async_accept(*p_socket, boost::bind(accept_handler, _1, p_socket));

  //  io_service.run();
  boost::thread_group threads;
  for (int i = 0; i < 4; ++i) {
	threads.create_thread([&io_service]() { io_service.run(); });
  }
  threads.join_all();
}