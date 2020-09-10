#ifndef _GROXY_RELAY_HPP
#define _GROXY_RELAY_HPP

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace logging = boost::log;
namespace keywords = boost::log::keywords;

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
	ssl_relay(asio::io_context &io, ssl::context &ctx, tcp::endpoint &remote) :
		_host_resolve(io), _strand(io), _raw_sock(io), _ssl_sock(io, ctx),
		_remote(remote), _raw_data(READ_BUFFER_SIZE, 0), _ssl_data(READ_BUFFER_SIZE, 0)
	{
		BOOST_LOG_TRIVIAL(debug) << "ssl relay construct";
	}
	~ssl_relay()  {
		BOOST_LOG_TRIVIAL(debug) << "ssl relay destruct";
	};

	void local_start();
	void remote_start();

	tcp::socket & get_raw_sock() {return _raw_sock;}
	ssl_socket & get_ssl_sock() {return _ssl_sock;}
private:
//	typedef struct { std::string in; std::string out; } data_t;

	asio::io_context::strand _strand;
	tcp::socket _raw_sock;
	ssl_socket  _ssl_sock;
	std::string _raw_data;
	std::string _ssl_data;
	tcp::endpoint _remote;
	tcp::resolver _host_resolve;

	void stop_relay();
  template <typename SOCK_R, typename SOCK_W>
	void read_data(SOCK_R &sock_r, SOCK_W &sock_w, std::string &data,
		       const boost::system::error_code &error, std::size_t len);
  template <typename SOCK_R, typename SOCK_W>
	void send_data(SOCK_R &sock_r, SOCK_W &sock_w, std::string &data,
		       const boost::system::error_code& error, std::size_t len);

	void local_on_start(const boost::system::error_code& error, std::size_t len);
	void local_start_ssl(const boost::system::error_code& error, std::size_t len);
	void on_ssl_connect(const boost::system::error_code& error);
	void start_relay(const boost::system::error_code &error, std::size_t len);

	void on_ssl_handshake(const boost::system::error_code& error);
	void on_read_sock5_cmd(const boost::system::error_code& error, std::size_t len);
	void on_remote_connect( const boost::system::error_code& error);

};

#endif
