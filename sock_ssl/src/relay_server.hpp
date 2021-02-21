#ifndef _GROXY_RELAY_SERVER_HPP
#define _GROXY_RELAY_SERVER_HPP
#include "relay.hpp"

class relay_server
{
public:
	relay_server(const relay_config &config):
		_io_context(),// _ctx(ssl::context::tlsv12_client),
		_config(config),
		_acceptor(_io_context, tcp::endpoint(tcp::v4(), config.local_port)),
		_remote(ip::make_address(config.remote_ip), config.remote_port),_timer(_io_context) {
	}

	// // local_server constructor
	// relay_server(int local_port, int remote_port, const std::string &remote_ip):
	//	_io_context(),// _ctx(ssl::context::tlsv12_client),
	//	_lport(local_port),
	//	_acceptor(_io_context), _remote(ip::make_address(remote_ip), remote_port),_timer(_io_context) {
	//							   }

	// // remote_server constructor
	// relay_server(int port):
	//	_io_context(),// _ctx(ssl::context::tlsv12_server),

	// }

	void local_server_start(std::shared_ptr<ssl_relay> ssl_ptr);
	void remote_server_start(std::shared_ptr<ssl_relay> ssl_ptr);

	void start_server();

	void run();// { _io_context.run(); }
//	void init_ssl();

private:
	relay_config _config;

	asio::io_context _io_context;
	tcp::acceptor _acceptor;
//	ssl::context _ctx;
	tcp::endpoint _remote;
	asio::steady_timer _timer;
//	std::shared_ptr<ssl_relay> _ssl_server;

	void remote_handle_accept(std::shared_ptr<ssl_relay> sock_ptr, const boost::system::error_code& error);
	void handle_timer(const boost::system::error_code& err);
	void local_handle_accept(std::shared_ptr<ssl_relay> ssl_ptr, std::shared_ptr<raw_relay> new_relay, const boost::system::error_code& error);

};

#endif
