#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <string>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <iomanip>
#include <ctime>
using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

// Open a socket and connect it to the remote host.
boost::asio::io_context io_context;

void show_time(const std::string &info)
{
	std::time_t t = std::time(nullptr);
	std::cout << info << std::put_time(std::localtime(&t), "%c %Z") << '\n';

}
int client(int port, int id)
{
	ssl::context ctx(ssl::context::tlsv12_client);
	ctx.set_default_verify_paths();

	boost::asio::io_context io_context;
	ssl_socket sock(io_context, ctx);

	tcp::endpoint server_ep(boost::asio::ip::make_address("127.0.0.1"), port);

	sock.lowest_layer().connect(server_ep);
	sock.lowest_layer().set_option(tcp::no_delay(true));

	// Perform SSL handshake and verify the remote host's certificate.
	sock.set_verify_mode(ssl::verify_peer);
	sock.handshake(ssl_socket::client);

	std::ostringstream out;
	out << "client "<<id;

	std::string w_buf = out.str();
	std::string r_buf(128,0);

	for (int count = 0;; count++) {
		int len = sock.write_some(boost::asio::buffer(w_buf));
		show_time("write at ");

//		std::cout << "count" << count <<":" << std::endl;
		len = sock.read_some(boost::asio::buffer(r_buf));
//		std::cout << "read:" <<len <<std::endl;
		show_time("read at ");
		std::cout << r_buf.substr(0,len) <<std::endl;
		sleep(id);

	}

	return 0;

}
