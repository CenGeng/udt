#include <boost/asio/io_service.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/log/trivial.hpp>
#include <boost/system/error_code.hpp>
#include <boost/thread.hpp>

#include "udt/connected_protocol/logger/file_log.h"
#include "udt/ip/udt.h"

boost::mutex rmtx; // Global mutex
boost::mutex wmtx; // Global mutex


int main(int argc, char *argv[]) {
  if (argc!=3) {
	BOOST_LOG_TRIVIAL(error) << "Command help : ./udt_client [host] [port]";
	return 1;
  }

  using udt_protocol = ip::udt<>;
  using Buffer = std::array<u_char, 100000>;
  using SendHandler =
	  std::function<void(const boost::system::error_code &, std::size_t)>;
  using ReceiveHandler =
	  std::function<void(const boost::system::error_code &, std::size_t)>;
  using ConnectHandler = std::function<void(const boost::system::error_code &)>;


  boost::asio::io_service io_service;
  boost::system::error_code resolve_ec;

  Buffer out_buffer1; // Buffer for sending data
  Buffer in_buffer1; // Buffer for receiving data

  std::string pid = std::to_string(getpid());

  std::string welcome = "client " + pid + " connected";	// Welcome message
  std::string bey = "bey" + pid;	// Goodbye message

  udt_protocol::resolver resolver(io_service);
  udt_protocol::socket socket(io_service);

  udt_protocol::resolver::query client_udt_query(argv[1], argv[2]);
  auto remote_endpoint_it = resolver.resolve(client_udt_query, resolve_ec);

  if (resolve_ec) {
	BOOST_LOG_TRIVIAL(error) << "Wrong arguments provided";
	return 1;
  }

  udt_protocol::endpoint remote_endpoint(*remote_endpoint_it);

  ConnectHandler connected;
  SendHandler sent_handler;
  ReceiveHandler receive_handler;

  connected = [&](const boost::system::error_code &ec) {
	if (!ec) {
	  BOOST_LOG_TRIVIAL(trace) << "Connected";
	  // Send the welcome message
	  std::copy(welcome.begin(), welcome.end(), out_buffer1.begin());
	  std::fill(out_buffer1.begin() + welcome.size(), out_buffer1.end(), 0);
	  socket.async_send(boost::asio::buffer(out_buffer1.data(),
											welcome.length()), sent_handler);
	  // Start an asynchronous read operation.
	  socket.async_receive(boost::asio::buffer(in_buffer1), receive_handler);
	} else {
	  BOOST_LOG_TRIVIAL(trace)
		<< "Error on connection : " << ec.value() << " " << ec.message();
	}
  };

  sent_handler = [&](const boost::system::error_code &ec, std::size_t length) {
	if (ec) {
	  BOOST_LOG_TRIVIAL(trace)
		<< "Error on sent : " << ec.value() << " " << ec.message();
	  return;
	}

	boost::lock_guard<boost::mutex> lock(wmtx); // Lock the mutex

	std::cout << "Sent " << length << " bytes" << std::endl;

	// Get user input save it to the in_buffer1
	std::string line;
	std::cout << "Please input: " << std::endl;
	std::getline(std::cin, line);

	// Ensure that the line is not larger than the buffer
	if (line.size() > out_buffer1.size()) {
	  line.resize(out_buffer1.size());
	}

	if(line != "exit"){
	  while (1) {
	  if (line.empty()) {
		std::cout << "Can NOT make the input NULL!" << std::endl
				  << "Please input again: ";
		std::getline(std::cin, line);
	  } else
		break;
	 }
	 // add the pid to the line
	 line = "client " + pid + ": " + line;

	 // Copy the line into the buffer
	 std::copy(line.begin(), line.end(), out_buffer1.begin());

	 // Fill the rest of the buffer with zeros
	 std::fill(out_buffer1.begin() + line.size(), out_buffer1.end(), 0);

	 // Send the data to the server
	 socket.async_send(boost::asio::buffer(out_buffer1.data(), line.length()), sent_handler);
	}else{

	  std::copy(bey.begin(), bey.end(), out_buffer1.begin());
	  std::fill(out_buffer1.begin() + bey.size(), out_buffer1.end(), 0);
	  socket.async_send(boost::asio::buffer(out_buffer1.data(),welcome.length()),
						[](const boost::system::error_code &ec, std::size_t length){
		std::cout << "Goodbye!" << std::endl;
		exit(1);
	  });
	}

  };

  receive_handler = [&](const boost::system::error_code &ec, std::size_t length) {
	if (ec) {
	  BOOST_LOG_TRIVIAL(trace)
		<< "Error on receive : " << ec.value() << " " << ec.message();
	  return;
	}

	boost::lock_guard<boost::mutex> lock(rmtx); // Lock the mutex

//	BOOST_LOG_TRIVIAL(trace) << "Received " << length << " bytes";

	// Print the received data.
	std::cout << std::string(in_buffer1.begin(), in_buffer1.begin() + length) << std::endl;

	std::cout << "Please input: " << std::endl;
	// Clear the buffer
	std::fill(in_buffer1.begin(), in_buffer1.end(), 0);
	std::fill(out_buffer1.begin(), out_buffer1.end(), 0);


	// Continue reading.
	socket.async_receive(boost::asio::buffer(in_buffer1), receive_handler);
  };

  socket.async_connect(remote_endpoint, connected);

//  io_service.run();
  boost::thread_group threads;
  for (uint16_t i = 1; i <= 4; ++i) {
	threads.create_thread([&io_service]() { io_service.run(); });
  }
  threads.join_all();

}