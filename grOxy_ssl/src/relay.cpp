#include "relay.hpp"

#include <iostream>
#include <vector>
#include <thread>

void ssl_relay::start_relay(const boost::system::error_code &error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "ssl handshake error: "<<error.message();
		stop_relay();
		return;
	}
//	boost::system::error_code init_err;
	read_data(_raw_sock, _ssl_sock, _raw_data, error , _raw_data.size());
	read_data(_ssl_sock, _raw_sock, _ssl_data, error, _ssl_data.size());

}
void ssl_relay::stop_relay()
{
	boost::system::error_code err;
	_raw_sock.close(err);
	_ssl_sock.shutdown(err);
	_ssl_sock.lowest_layer().close(err);
}
template <typename SOCK_R, typename SOCK_W>
void ssl_relay::read_data(SOCK_R &sock_r, SOCK_W &sock_w, std::string &data,
			  const boost::system::error_code &error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on write error: "<<error.message();
		stop_relay();
		return;
	}
	if (len != data.size()) {
		BOOST_LOG_TRIVIAL(info) << "send len "<<len <<" not match buf len "<<data.size();
		stop_relay();
		return;
	}
	data.resize(READ_BUFFER_SIZE);
	sock_r.async_read_some(asio::buffer(data),
			       asio::bind_executor(_strand,
						   std::bind(&ssl_relay::send_data<SOCK_R,SOCK_W>,
							     shared_from_this(),
							     std::ref(sock_r), std::ref(sock_w), std::ref(data),
							     std::placeholders::_1, std::placeholders::_2)));
}
template <typename SOCK_R, typename SOCK_W>
void ssl_relay::send_data(SOCK_R &sock_r, SOCK_W &sock_w, std::string &data,
			  const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on read error: "<<error.message();
		stop_relay();
		return;
	}
	if (len == 0) {
		data.resize(0);
		boost::system::error_code init_err;
		read_data(sock_r, sock_w, data, init_err, 0);
		return;
	}
	data.resize(len);
	async_write(sock_w, asio::buffer(data),
		    asio::bind_executor(_strand,
					std::bind(&ssl_relay::read_data<SOCK_R,SOCK_W>,
						  shared_from_this(),
						  std::ref(sock_r), std::ref(sock_w), std::ref(data),
						  std::placeholders::_1, std::placeholders::_2)));
}
void ssl_relay::on_ssl_connect(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on ssl connect error: "<<error.message();
		stop_relay();
		return;
	}

	_ssl_sock.lowest_layer().set_option(tcp::no_delay(true));
	_ssl_sock.async_handshake(ssl_socket::client,
				  asio::bind_executor(_strand,
						      std::bind(&ssl_relay::start_relay, shared_from_this(),
								std::placeholders::_1, 0)));
}
void ssl_relay::local_start_ssl(const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "write init sock5 ack error: "<<error.message();
		stop_relay();
		return;
	}

	_ssl_sock.lowest_layer().async_connect(_remote,
					       std::bind(&ssl_relay::on_ssl_connect, shared_from_this(),
							 std::placeholders::_1));
}
void ssl_relay::local_on_start(const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on start read error: "<<error.message();
		stop_relay();
		return;
	}

	_raw_data[0] = 5;
	_raw_data[1] = 0;
	async_write(_raw_sock, asio::buffer(_raw_data, 2),
		    std::bind(&ssl_relay::local_start_ssl, shared_from_this(),
			      std::placeholders::_1, std::placeholders::_2));

}
void ssl_relay::local_start()
{
	_raw_sock.async_receive(asio::buffer(_raw_data),
				std::bind(&ssl_relay::local_on_start, shared_from_this(),
					  std::placeholders::_1, std::placeholders::_2));
}

void ssl_relay::on_remote_connect( const boost::system::error_code& error)
{
	if (error ) {
		BOOST_LOG_TRIVIAL(info) << "remote connect error: "<<error.message();
		stop_relay();
		return;
	}

	uint8_t ret_val[] =  {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
	std::copy(std::begin(ret_val), std::end(ret_val), _ssl_data.begin());
	asio::async_write(_ssl_sock, asio::buffer(_ssl_data, 10),
			  asio::bind_executor(_strand,
					      std::bind(&ssl_relay::start_relay, shared_from_this(),
							std::placeholders::_1, std::placeholders::_2)));
}

void ssl_relay::on_read_sock5_cmd(const boost::system::error_code& error, std::size_t len)
{
	if (error || len <= 6 ||_ssl_data[1] != 1) {	// not connect cmd, error
		BOOST_LOG_TRIVIAL(info) << "read sock5 cmd error: "<<error.message();
		stop_relay();
		return;
	}

	uint8_t *port = nullptr;
	switch (_ssl_data[3]) {
	case 1:{
		ip::address_v4::bytes_type *addr_4 = (ip::address_v4::bytes_type *)(&_ssl_data[4]);
		if (len < sizeof(*addr_4) + 6) {
			BOOST_LOG_TRIVIAL(info) << "sock5 addr4 len error: ";
			stop_relay();
			return;
		}
		_remote.address(ip::make_address_v4(*addr_4));
		port = (uint8_t*)&addr_4[1];
		_remote.port(port[0]<<8 | port[1]);
		_raw_sock.async_connect(_remote,
					std::bind(&ssl_relay::on_remote_connect, shared_from_this(),
						  std::placeholders::_1));
		break;
	}

	case 4:
	{
		ip::address_v6::bytes_type *addr_6 = (ip::address_v6::bytes_type *)(&_ssl_data[4]);
		if (len < sizeof(*addr_6) + 6) {
			BOOST_LOG_TRIVIAL(info) << "sock5 addr6 len error: ";
			stop_relay();
			return;
		}
		_remote.address(ip::make_address_v6(*addr_6));
		port = (uint8_t*)&addr_6[1];
		_remote.port(port[0]<<8 | port[1]);
		_raw_sock.async_connect(_remote,
					std::bind(&ssl_relay::on_remote_connect, shared_from_this(),
						  std::placeholders::_1));
		break;

	}
	case 3: {
		int host_len = _ssl_data[4];
		if ( len < host_len + 1 +6) {
			BOOST_LOG_TRIVIAL(info) << "sock5 host name len error: ";
			stop_relay();
			return;
		}
		std::string host_name = _ssl_data.substr(5, host_len);

		port = (uint8_t*)&_ssl_data[host_len+1+4];

		std::ostringstream port_name;
		port_name << ((port[0]<<8)|port[1]);
		BOOST_LOG_TRIVIAL(debug) << "host " <<host_name <<" port "<<port_name.str() ;
		boost::system::error_code ec;
		auto re_hosts = _host_resolve.resolve(host_name, port_name.str(), ec);
		if (ec) {
			BOOST_LOG_TRIVIAL(debug) << "host resolve error";
			return;

		}
		asio::async_connect(_raw_sock, re_hosts,
				    std::bind(&ssl_relay::on_remote_connect, shared_from_this(),
					      std::placeholders::_1));
		break;
	}

	default:
		BOOST_LOG_TRIVIAL(info) << "sock5 cmd type not support ";
		break;
	}
}


void ssl_relay::on_ssl_handshake(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "remote handshake error: "<<error.message();
		stop_relay();
		return;
	}
	_ssl_sock.async_read_some(asio::buffer(_ssl_data),
				  std::bind(&ssl_relay::on_read_sock5_cmd, shared_from_this(),
					    std::placeholders::_1, std::placeholders::_2));
}

void ssl_relay::remote_start()
{
	_ssl_sock.lowest_layer().set_option(tcp::no_delay(true));
	_ssl_sock.async_handshake(ssl_socket::server,
				  std::bind(&ssl_relay::on_ssl_handshake, shared_from_this(),
					    std::placeholders::_1));
}
