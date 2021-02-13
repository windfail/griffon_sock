#ifndef _GROXY_RELAY_SERVER_HPP
#define _GROXY_RELAY_SERVER_HPP
#include "relay.hpp"

class relay_server
{
public:
	// local_server constructor
	relay_server(int local_port, int remote_port, const std::string &remote_ip):
		_io_context(), _ctx(ssl::context::tlsv12_client), _acceptor(_io_context, tcp::endpoint(tcp::v4(), local_port)), _remote(ip::make_address(remote_ip), remote_port) {
	}

	// remote_server constructor
	relay_server(int port):
		_io_context(), _ctx(ssl::context::tlsv12_server), _acceptor(_io_context, tcp::endpoint(tcp::v4(), port)) {
	}

	void local_start_accept();
	void local_handle_accept( std::shared_ptr<ssl_relay> sock_ptr, const boost::system::error_code& error);
	void remote_start_accept();
	void remote_handle_accept(std::shared_ptr<ssl_relay> sock_ptr, const boost::system::error_code& error);
	void run() { _io_context.run(); }
	void init_ssl();

private:
	asio::io_context _io_context;
	tcp::acceptor _acceptor;
	ssl::context _ctx;
	tcp::endpoint _remote;
};

#endif
