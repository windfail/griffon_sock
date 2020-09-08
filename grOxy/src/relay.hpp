#ifndef _GROXY_RELAY_HPP
#define _GROXY_RELAY_HPP

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>

namespace asio = boost::asio;
namespace ip = boost::asio::ip;
using boost::asio::ip::tcp;

namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

const int READ_BUFFER_SIZE = 4096;

class ssl_relay
	:public std::enable_shared_from_this<ssl_relay>
{
public:
	ssl_relay(asio::io_context &io, ssl::context &ctx) : _strand(io), _raw_sock(io), _ssl_sock(io, ctx)
	{}
	~ssl_relay()  {};
	void start_relay();
	tcp::socket & get_raw_sock() {return _raw_sock;}
	ssl_socket & get_ssl_sock() {return _ssl_sock;}
private:
//	typedef struct { std::string in; std::string out; } data_t;

	asio::io_context::strand _strand;
	tcp::socket _raw_sock;
	ssl_socket  _ssl_sock;
	std::string _raw_data;
	std::string _ssl_data;
	void stop_relay();
  template <typename SOCK_R, typename SOCK_W>
	void read_data(SOCK_R &sock_r, SOCK_W &sock_w, std::string &data,
		       const boost::system::error_code &error, std::size_t len);
  template <typename SOCK_R, typename SOCK_W>
	void send_data(SOCK_R &sock_r, SOCK_W &sock_w, std::string &data,
		       const boost::system::error_code& error, std::size_t len);

};

#endif
