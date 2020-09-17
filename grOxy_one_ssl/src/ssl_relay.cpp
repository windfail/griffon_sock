#include "relay.hpp"

#include <iostream>

#if 0

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
#endif
// ok begin
void ssl_relay::ssl_stop_raw_relay(std::shared_ptr<raw_relay> &relay)
{
	if (!relay) {
		return;
	}
	auto stop_raw = std::bind(&raw_relay::stop_raw_relay, relay, relay_data::from_ssl);
	relay->get_strand().dispatch(stop_raw, asio::get_associated_allocator(stop_raw));
	relay.reset();

	return;

}
// if stop cmd is from raw relay, send stop cmd to ssl
void ssl_relay::stop_ssl_relay(uint32_t index, relay_data::stop_src src)
{
	if (index >= _relays.size()) {
		// wrong index
	}
	if (src == relay_data::ssl_err) {
		// stop all raw_relay
		for (auto & relay : _relays) {
			ssl_stop_raw_relay(relay.relay);
		}
	}

	if (src == relay_data::from_raw) {
		_relays[index].relay.reset();
		// send to ssl
		auto buffer = std::make_shared<relay_data>(index, relay_data::STOP_RELAY);
		async_write(_sock, buffer->buffers(),
			    asio::bind_executor(_strand,
						std::bind(&ssl_relay::on_write_ssl, this, buffer,
							  std::placeholders::_1, std::placeholders::_2)));

	}
	if (src == relay_data::from_ssl) {
		ssl_stop_raw_relay(_relays[index].relay);
	}
}

void ssl_relay::ssl_data_relay(std::shared_ptr<relay_data> w_data)
{
	async_write(_sock, w_data->buffers(),
		    asio::bind_executor(_strand,
					std::bind(&ssl_relay::on_write_ssl, this, w_data,
						  std::placeholders::_1, std::placeholders::_2)));
}
void ssl_relay::on_write_ssl(std::shared_ptr<relay_data> w_data, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on ssl write error: "<<error.message();
		stop_ssl_relay(w_data->head().index, relay_data::ssl_err);
		return;
	}
	if (len != w_data->size()) {
		BOOST_LOG_TRIVIAL(info) << "on ssl write error: "<<error.message();
		stop_ssl_relay(w_data->head().index, relay_data::ssl_err);
		return;
	}

}

// send start cmd to remote to start index
// void ssl_relay::start_ssl_relay(int index)
// {
//	auto buffer = std::make_shared<relay_data>(index, relay_data::START_RELAY);

//	async_write(_sock, buffer->buffers(),
//		    asio::bind_executor(_strand,
//					std::bind(&ssl_relay::on_write_ssl, this, buffer,
//						  std::placeholders::_1, std::placeholders::_2)));

// }

// call add_new_relay, vector access, must run in ssl_relay strand
void ssl_relay::local_handle_accept(std::shared_ptr<raw_relay> relay, const boost::system::error_code& error)
{
	local_start_accept();
	if (error) {
		BOOST_LOG_TRIVIAL(debug) <<" handle accept error "<<std::endl;
		return;
	}
	int index = add_new_relay(relay);
	// send start index cmd to ssl
//	auto task_ssl = std::bind(&ssl_relay::start_ssl_relay, this, index);
//	_strand.post(task_ssl, asio::get_associated_allocator(task_ssl));

	auto task = std::bind(&raw_relay::local_start, relay);
	relay->get_strand().dispatch(task, asio::get_associated_allocator(task));

	//relay->local_start();
}
void ssl_relay::local_start_accept()
{
	auto relay = std::make_shared<raw_relay> (_io_context, shared_from_this());

	_acceptor.async_accept(relay->get_sock(),
			       asio::bind_executor(_strand,
						   std::bind(&ssl_relay::local_handle_accept,
							     this, relay, std::placeholders::_1)));
}

uint32_t ssl_relay::add_new_relay(const std::shared_ptr<raw_relay> &relay)
{
	auto val = find_if(_relays.begin(), _relays.end(), [](const _relay_t &tmp) { return tmp.session == 0; });
	if (val == _relays.end()) {
		relay->index( _relays.size());
		_relays.push_back({60, relay, 60});
	} else {
		relay->index(val - _relays.begin());
		*val = {60, relay, 60};
	}
	return relay->index();

}

// send data on ssl sock
// buf is read from sock in raw_relay
void ssl_relay::send_data_on_ssl(std::shared_ptr<relay_data> buf)
{
	async_write(_sock, buf->buffers(),
		    asio::bind_executor(
			    _strand,
			    std::bind(&ssl_relay::on_write_ssl, this, buf,
				      std::placeholders::_1, std::placeholders::_2)));
}

void ssl_relay::on_read_ssl_data(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on read ssl data error: "<<error.message();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	if (len != buf->data_size()) {
		BOOST_LOG_TRIVIAL(info) << "on read ssl data len "<< len <<" expect "<<buf->data().size();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	auto index = buf->head().index;
	switch ( buf->head().cmd) {
	case relay_data::DATA_RELAY: {
		if (index > _relays.size()) {
			start_ssl_data_relay();
			return;
		}
		auto raw_data_send = std::bind(&raw_relay::send_data_on_raw, _relays[index].relay, buf);
		_relays[index].relay->get_strand().dispatch(raw_data_send, asio::get_associated_allocator(raw_data_send));

		break;
	}
	case relay_data::START_RELAY: {
		// remote get start connect
		auto relay = std::make_shared<raw_relay> (_io_context, shared_from_this());

		int index = add_new_relay(relay);
		auto start_task = std::bind(&raw_relay::start_remote_connect, relay, buf);
		relay->get_strand().dispatch(start_task, asio::get_associated_allocator(start_task));
		break;
	}
	}
	start_ssl_data_relay();
}
void ssl_relay::on_read_ssl_header(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{
	auto index = buf->head().index;
	if (error
	    || len != sizeof(relay_data::_header_t)
	    || buf->head().len > READ_BUFFER_SIZE) {
		BOOST_LOG_TRIVIAL(info) << "on read ssl header error\n\t"<<error.message();
		BOOST_LOG_TRIVIAL(info) << "\theader len: "<<len << " expect "<<sizeof(relay_data::_header_t);
		BOOST_LOG_TRIVIAL(info) << "\tdata len: "<<buf->head().len;
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	buf->resize();

	auto session = buf->head().session;

	if (buf->data_size() != 0) {
		async_read(_sock, buf->data_buffer(),
			   asio::bind_executor(
				   _strand,
				   std::bind(&ssl_relay::on_read_ssl_data, this, buf,
					     std::placeholders::_1, std::placeholders::_2)));
		return;
	}

	if (index >= _relays.size()
	    || session != _relays[index].session) {
		// discard
		start_ssl_data_relay();
		return;
	}
	switch (buf->head().cmd) {
	case relay_data::START_RELAY: {
		// local get start from remote, tell raw relay begin
		auto start_task = std::bind(&raw_relay::start_data_relay, _relays[index].relay);
		_relays[index].relay->get_strand().dispatch(start_task, asio::get_associated_allocator(start_task));
		break;
	}
	case relay_data::STOP_RELAY:
		// dispatch stop to raw
		stop_ssl_relay(index, relay_data::from_ssl);
		break;
	}
	start_ssl_data_relay();
}

// start ssl data relay
void ssl_relay::start_ssl_data_relay()
{
	auto buf = std::make_shared<relay_data>(0, relay_data::DATA_RELAY);
	_sock.async_read_some(
		buf->header_buffer(),
		asio::bind_executor(
			_strand,
			std::bind(&ssl_relay::on_read_ssl_header, this, buf,
				  std::placeholders::_1, std::placeholders::_2)));
}
