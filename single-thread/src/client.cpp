#include <boost/asio.hpp>
#include <string>
#include <iostream>
#include <unistd.h>
using boost::asio::ip::tcp;

int client(int port)
{
	std::string buf(128,0);

	boost::asio::io_context io_context;
	tcp::socket socket(io_context);
	tcp::endpoint server_ep(boost::asio::ip::make_address("127.0.0.1"), port);

	socket.connect(server_ep);
	socket.read_some(boost::asio::buffer(buf));
	std::cout << buf <<std::endl;
	pause();

	return 0;

}
