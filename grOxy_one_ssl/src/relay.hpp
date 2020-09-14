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

class ssl_relay;

// raw relay , for client to local server and remote server to dest
class raw_relay
	:public std::enable_shared_from_this<raw_relay>
{
public:
	int index;
	asio::io_context::strand _strand;

	raw_relay(asio::io_context &io, const std::shared_ptr<ssl_relay> &manager) :
		index (0), _strand(io), _sock(io), _host_resolve(io), _manager(manager)
		{
		}

	void local_start();
	void local_on_start(const boost::system::error_code& error, std::size_t len);
	tcp::socket & get_sock() {return _sock;}
private:
	std::string _data;
	tcp::socket _sock;
	tcp::resolver _host_resolve;
	std::shared_ptr<ssl_relay> _manager;
	void start_relay(const boost::system::error_code& error, std::size_t len);

};

// ssl relay , maintain tls between local server and remote server
class ssl_relay
	:public std::enable_shared_from_this<ssl_relay>
{
public:
	asio::io_context::strand _strand;
	ssl_relay(asio::io_context &io, ssl::context &ctx, tcp::endpoint &remote, int local_port) :
		_strand(io), _sock(io, ctx),_acceptor(_io_context, tcp::endpoint(tcp::v4(), local_port)),
		_remote(remote)
	{
		BOOST_LOG_TRIVIAL(debug) << "ssl relay construct";
	}
	~ssl_relay()  {
		BOOST_LOG_TRIVIAL(debug) << "ssl relay destruct";
	};

//	void local_start();
//	void remote_start();


//	ssl_socket & get_ssl_sock() {return _ssl_sock;}
private:
//	typedef struct { std::shared_ptr<raw_relay> relay; } _relay_t;
	asio::io_context _io_context;

	tcp::acceptor _acceptor;
	ssl_socket  _sock;
	std::vector<std::shared_ptr<raw_relay>> _relays;

	tcp::endpoint _remote;
	int add_new_relay(const std::shared_ptr<raw_relay> &relay);

//	void stop_relay();


	void local_start_accept();
	void local_handle_accept(std::shared_ptr<raw_relay> relay, const boost::system::error_code& error);

	// void local_on_start(const boost::system::error_code& error, std::size_t len);
	// void local_start_ssl(const boost::system::error_code& error, std::size_t len);
	// void on_ssl_connect(const boost::system::error_code& error);
	// void start_relay(const boost::system::error_code &error, std::size_t len);

	// void on_ssl_handshake(const boost::system::error_code& error);
	// void on_read_sock5_cmd(const boost::system::error_code& error, std::size_t len);
	// void on_remote_connect( const boost::system::error_code& error);



};

#endif