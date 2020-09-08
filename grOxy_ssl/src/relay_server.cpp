#include "relay_server.hpp"

void relay_server::local_handle_accept( std::shared_ptr<ssl_relay> sock_ptr, const boost::system::error_code& error)
{
	local_start_accept();
	if (error) {
		BOOST_LOG_TRIVIAL(debug) <<" handle accept error "<<std::endl;
		return;
	}
	try {
		std::string _data_r;

		_data_r.resize(512);
		sock_ptr->get_raw_sock().receive(asio::buffer(_data_r));
		uint8_t sock_ok[] = {5,0};
		sock_ptr->get_raw_sock().send(asio::buffer(sock_ok));

		ssl_socket &remote_sock = sock_ptr->get_ssl_sock();
		remote_sock.lowest_layer().connect(_remote);
		remote_sock.lowest_layer().set_option(tcp::no_delay(true));
		remote_sock.handshake(ssl_socket::client);
	} catch(std::runtime_error& error) {
		BOOST_LOG_TRIVIAL(info) <<"prepare throw exception: " << error.what()<<std::endl;
		return;
	}

	sock_ptr->start_relay();
}

void relay_server::local_start_accept()
{
	auto sock_ptr = std::make_shared<ssl_relay> (_io_context, _ctx);

	_acceptor.async_accept(sock_ptr->get_raw_sock(),
			       std::bind(&relay_server::local_handle_accept, this, sock_ptr, std::placeholders::_1));
}

void relay_server::init_ssl()
{
	_ctx.load_verify_file("yily.crt");
	_ctx.set_verify_mode(ssl::verify_peer|ssl::verify_fail_if_no_peer_cert);
	_ctx.use_certificate_file("yily.crt", ssl::context::pem);
	_ctx.use_rsa_private_key_file("key.pem", ssl::context::pem);
}


static int get_port(ssl_socket &ssl_sock)
{
	std::array<uint8_t, 2> sock_data;
	ssl_sock.read_some(asio::buffer(sock_data));
	return (sock_data[0]<<8) | sock_data[1];
}

static void sock5_connect_host(asio::io_context &io, tcp::socket &remote_sock, ssl_socket &ssl_sock)
{
	std::string _data_r(512,0);
	int len = ssl_sock.read_some(asio::buffer(_data_r, 4));
	if (len != 4 ||_data_r[1] != 1) {	// not connect cmd, error
		throw(std::runtime_error("sock5 receive connect command fail"));
	}
	auto type = _data_r[3];
	std::string host_name;
	tcp::endpoint remote;
	switch (type) {
	case 1:{
		ip::address_v4::bytes_type addr_4;
		asio::read(ssl_sock, asio::buffer(addr_4));
		remote.address(ip::make_address_v4(addr_4));
		remote.port(get_port(ssl_sock));
		remote_sock.connect(remote);
		break;
	}
	case 4:{
		ip::address_v6::bytes_type addr_6;
		asio::read(ssl_sock, asio::buffer(addr_6));
		remote.address(ip::make_address_v6(addr_6));
		remote.port(get_port(ssl_sock));
		remote_sock.connect(remote);
		break;
	}
	case 3:{
		asio::read(ssl_sock,asio::buffer(_data_r,1));
		len = _data_r[0];
		if (len != ssl_sock.read_some(asio::buffer(_data_r,len))) {
			throw(std::runtime_error("sock5 receive host name fail"));
		}
		host_name = _data_r.substr(0, len);
		tcp::resolver host_resolve(io);
		std::ostringstream port_name;
		port_name << get_port(ssl_sock);
//		BOOST_LOG_TRIVIAL(debug) << "host " <<host_name <<" port "<<port_name.str() <<std::endl;
		auto re_hosts = host_resolve.resolve(host_name, port_name.str());
		asio::connect(remote_sock, re_hosts);

		break;
	}
	default:
		throw(std::runtime_error("sock5 cmd not support"));

	}
	uint8_t ret_val[] =  {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
	asio::write(ssl_sock, asio::buffer(ret_val));
}
void relay_server::remote_handle_accept(std::shared_ptr<ssl_relay> sock_ptr, const boost::system::error_code& error)
{
	remote_start_accept();
	if (error) {
		return;
	}
	try {
		sock_ptr->get_ssl_sock().lowest_layer().set_option(tcp::no_delay(true));
		sock_ptr->get_ssl_sock().handshake(ssl_socket::server);

		sock5_connect_host(_io_context, sock_ptr->get_raw_sock(), sock_ptr->get_ssl_sock());
	} catch(std::runtime_error& error) {
		BOOST_LOG_TRIVIAL(info) <<"prepare throw exception: " << error.what()<<std::endl;
		return;
	}
	sock_ptr->start_relay();
}
void relay_server::remote_start_accept()
{
	auto sock_ptr = std::make_shared<ssl_relay> (_io_context, _ctx);
	_acceptor.async_accept(sock_ptr->get_ssl_sock().lowest_layer(),
			       std::bind(&relay_server::remote_handle_accept, this, sock_ptr, std::placeholders::_1));
}
