#include "relay.hpp"

#include <iostream>
// ok begin common ssl relay functions
ssl_relay::_relay_t::~_relay_t()
{
	if (relay->session() == 0) {
		return;
	}
	auto stop_raw = std::bind(&raw_relay::stop_raw_relay, relay, relay_data::from_ssl);
	relay->get_strand().dispatch(stop_raw, asio::get_associated_allocator(stop_raw));

}

// if stop cmd is from raw relay, send stop cmd to ssl
void ssl_relay::stop_ssl_relay(uint32_t session, relay_data::stop_src src)
{
	if (src == relay_data::ssl_err) {
		// stop all raw_relay
		if (_started) {
			_relays.clear();
			boost::system::error_code err;
			_sock.shutdown(err);
			_sock.lowest_layer().close(err);
			_started = false;
		}
		return;
	}

	_relays.erase(session);
	if (src == relay_data::from_raw) {
		// send to ssl
		auto buffer = std::make_shared<relay_data>(session, relay_data::STOP_RELAY);
		async_write(_sock, buffer->buffers(),
			    asio::bind_executor(_strand,
						std::bind(&ssl_relay::on_write_ssl, shared_from_this(), buffer,
							  std::placeholders::_1, std::placeholders::_2)));
	}
}

void ssl_relay::on_write_ssl(std::shared_ptr<relay_data> w_data, const boost::system::error_code& error, std::size_t len)
{
	if (error
	    || len != w_data->size()) {
		BOOST_LOG_TRIVIAL(info) << "on ssl write error: "<<error.message();
		BOOST_LOG_TRIVIAL(info) << "\tlen: "<<len << " data size "<<w_data->size();
		stop_ssl_relay(w_data->head().session, relay_data::ssl_err);
		return;
	}

}

// send data on ssl sock
// buf is read from sock in raw_relay
void ssl_relay::send_data_on_ssl(std::shared_ptr<relay_data> buf)
{

	async_write(_sock, buf->buffers(),
		    asio::bind_executor(
			    _strand,
			    std::bind(&ssl_relay::on_write_ssl, shared_from_this(), buf,
				      std::placeholders::_1, std::placeholders::_2)));

}

void ssl_relay::on_read_ssl_data(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error
	    || len != buf->data_size()) {
		BOOST_LOG_TRIVIAL(info) << "on read ssl data error: "<<error.message();
		BOOST_LOG_TRIVIAL(info) << "on read ssl data len "<< len <<" expect "<<buf->data().size();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}

	auto session = buf->head().session;
	switch ( buf->head().cmd) {
	case relay_data::DATA_RELAY: {
		auto val = _relays.find(session);

		if (val == _relays.end()) {
			// session stopped, 
			stop_ssl_relay(session, relay_data::from_raw);

		} else {
			auto relay = val->second.relay;
			auto raw_data_send = std::bind(&raw_relay::send_data_on_raw, relay, buf);
			relay->get_strand().dispatch(raw_data_send, asio::get_associated_allocator(raw_data_send));
		}

		break;
	}
	case relay_data::START_RELAY: {
		// remote get start connect
		auto relay = std::make_shared<raw_relay> (_io_context, shared_from_this());
		_relays[session] = relay;
		auto start_task = std::bind(&raw_relay::start_remote_connect, relay, buf);
		relay->get_strand().dispatch(start_task, asio::get_associated_allocator(start_task));
		break;
	}
	}
	start_ssl_data_relay();
}
void ssl_relay::on_read_ssl_header(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{

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
				   std::bind(&ssl_relay::on_read_ssl_data, shared_from_this(), buf,
					     std::placeholders::_1, std::placeholders::_2)));
		return;
	}


	switch (buf->head().cmd) {
	case relay_data::START_RELAY: {

		auto val = _relays.find(session);
		if (val == _relays.end() ) {
			// local stopped before remote connect, tell remote to stop
			stop_ssl_relay(session, relay_data::from_raw);
		} else {
			// local get start from remote, tell raw relay begin
			auto relay = val->second.relay;
			auto start_task = std::bind(&raw_relay::start_data_relay, relay);
			relay->get_strand().dispatch(start_task, asio::get_associated_allocator(start_task));
		}
		break;
	}
	case relay_data::STOP_RELAY:
		// dispatch stop to raw
		_relays.erase(session);
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
			std::bind(&ssl_relay::on_read_ssl_header, shared_from_this(), buf,
				  std::placeholders::_1, std::placeholders::_2)));
}
// remote ssl relay server functions
void ssl_relay::on_ssl_handshake(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "remote handshake error: "<<error.message();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	_started = true;
	start_ssl_data_relay();
}

void ssl_relay::remote_ssl_start()
{
	_sock.lowest_layer().set_option(tcp::no_delay(true));
	_sock.async_handshake(ssl_socket::server,
			      std::bind(&ssl_relay::on_ssl_handshake, shared_from_this(),
					std::placeholders::_1));

}

// local ssl relay server functions
// call add_new_relay, vector access, must run in ssl_relay strand
void ssl_relay::local_handle_accept(std::shared_ptr<raw_relay> relay, const boost::system::error_code& error)
{
	local_start_accept();
	if (error) {
		BOOST_LOG_TRIVIAL(debug) <<" handle accept error "<<std::endl;
		return;
	}

	add_new_relay(relay);

	auto task = std::bind(&raw_relay::local_start, relay);
	relay->get_strand().dispatch(task, asio::get_associated_allocator(task));
}
void ssl_relay::local_start_accept()
{
	// accept raw relay
	auto relay = std::make_shared<raw_relay> (_io_context, shared_from_this());
	_acceptor.async_accept(relay->get_sock(),
			       asio::bind_executor(_strand,
						   std::bind(&ssl_relay::local_handle_accept,
							     shared_from_this(), relay, std::placeholders::_1)));
}

uint32_t ssl_relay::add_new_relay(const std::shared_ptr<raw_relay> &relay)
{
	uint32_t session = 0;
	do {
		auto ran = _rand();
//		auto tmp = std::chrono::system_clock::now().time_since_epoch().count();
		auto tmp = time(nullptr);

		session = (ran & 0xffff0000) | (tmp & 0xffff);
		BOOST_LOG_TRIVIAL(info) << " new session: "<<session;
	} while ( _relays.count(session) );

	relay->session(session);
	_relays.emplace(session, relay);
	return session;
}

void ssl_relay::start_new_relay(std::shared_ptr<relay_data> buf)
{
	if (_started) {
		BOOST_LOG_TRIVIAL(info) << "ssl connected, start data send ";
		send_data_on_ssl(buf);
		return;
	}
	// connect ssl to remote
	// here use sync call to block other connect, only on ssl connect need to be done
	BOOST_LOG_TRIVIAL(info) << "start ssl connect : ";
	_started = true;
	boost::system::error_code error;
	_sock.lowest_layer().connect(_remote, error);
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "ssl connect error :" <<error.message();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	_sock.lowest_layer().set_option(tcp::no_delay(true));
	_sock.handshake(ssl_socket::client, error);
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "ssl handshake error :" <<error.message();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	send_data_on_ssl(buf);
	start_ssl_data_relay();

}

