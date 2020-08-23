#include <boost/asio.hpp>
#include <string>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <iomanip>
#include <ctime>
using boost::asio::ip::tcp;
void show_time(const std::string &info)
{
	std::time_t t = std::time(nullptr);
	std::cout << info << std::put_time(std::localtime(&t), "%c %Z") << '\n';

}
int client(int port, int id)
{
	boost::asio::io_context io_context;
	tcp::socket socket(io_context);
	tcp::endpoint server_ep(boost::asio::ip::make_address("127.0.0.1"), port);

	socket.connect(server_ep);
	std::ostringstream out;
	out << "client "<<id;

	std::string w_buf = out.str();
	std::string r_buf(128,0);

	for (int count = 0;; count++) {
		int len = socket.write_some(boost::asio::buffer(w_buf));
		show_time("write at ");

//		std::cout << "count" << count <<":" << std::endl;
		len = socket.read_some(boost::asio::buffer(r_buf));
//		std::cout << "read:" <<len <<std::endl;
		show_time("read at ");
		std::cout << r_buf.substr(0,len) <<std::endl;
		sleep(id);

	}

	return 0;

}
