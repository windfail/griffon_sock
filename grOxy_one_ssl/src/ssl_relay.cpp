#include "relay.hpp"

#include <iostream>
// ok begin common ssl relay functions
std::string buf_to_string(void *buf, std::size_t size);

class ssl_relay::_relay_t {
public:
	std::shared_ptr<raw_relay> relay;
	int timeout {TIMEOUT};
	_relay_t() {
	};
	_relay_t(const std::shared_ptr<raw_relay> &relay) :relay(relay) {};
	~_relay_t();
};

ssl_relay::_relay_t::~_relay_t()
{
	if (relay->session() == 0) {
		return;
	}
	auto stop_raw = std::bind(&raw_relay::stop_raw_relay, relay, relay_data::from_ssl);
	relay->get_strand().post(stop_raw, asio::get_associated_allocator(stop_raw));

}
void ssl_relay::timer_handle()
{
	BOOST_LOG_TRIVIAL(info) << " ssl relay timer handle : ";
	for (auto & rel : _relays) {
		if (--rel.second->timeout == 0) {
			BOOST_LOG_TRIVIAL(info) << " timeout stop : "<<rel.first;
			_relays.erase(rel.first);
		}
	}
}
void on_ssl_shutdown(const boost::system::error_code& error)
{
	BOOST_LOG_TRIVIAL(info) << "ssl shutdown over";

}
// if stop cmd is from raw relay, send stop cmd to ssl
void ssl_relay::stop_ssl_relay(uint32_t session, relay_data::stop_src src)
{
	if (src == relay_data::ssl_err) {
		// stop all raw_relay
		BOOST_LOG_TRIVIAL(info) << "ssl stop all relay :"<< _relays.size()<<" _relays "<<_bufs.size() <<"bufs ";
		_relays.clear();
		_bufs = std::queue<std::shared_ptr<relay_data>> ();
		boost::system::error_code err;
		if (_ssl_status == SSL_START) {
			_sock->async_shutdown(on_ssl_shutdown);
		}
		_sock->lowest_layer().shutdown(tcp::socket::shutdown_both, err);
		_sock->lowest_layer().close(err);
		_ssl_status = NOT_START;
//		auto lport =_acceptor.local_endpoint().port();
//		_acceptor.close(err);
		BOOST_LOG_TRIVIAL(info) << "ssl stop over";
		if (_config.local) {
			_ctx = init_ssl(_config);
			_sock = std::make_unique<ssl_socket>(*_io_context, _ctx);

//			_sock->~stream();
//			_sock->stream(tcp::socket(*_io_context), _ctx);


//			ssl::context ctx(ssl::context::tlsv12_client);
//			init_ssl(ctx);
			// auto new_server = std::make_shared<ssl_relay> (
			// 	_io_context,
			// 	init_ssl(ssl::context::tlsv12_client),
			// 	_remote, lport);
			// new_server->local_start_accept();
			// new_server->gfw = std::move(gfw);
		}

		return;
	}

	_relays.erase(session);
	if (src == relay_data::from_raw) {
		// send to ssl
		// BOOST_LOG_TRIVIAL(info) << " send raw stop : "<<session;
		auto buffer = std::make_shared<relay_data>(session, relay_data::STOP_RELAY);
		send_data_on_ssl(buffer);
	}
}

void ssl_relay::on_write_ssl(/*std::shared_ptr<relay_data> w_data, */const boost::system::error_code& error, std::size_t len)
{
	auto buf = _bufs.front();
	if (error
	    || len != buf->size()) {
		BOOST_LOG_TRIVIAL(error) << "on ssl write error: "<<error.message();
		BOOST_LOG_TRIVIAL(error) << "\tlen: "<<len << " data size "<<buf->size();
		stop_ssl_relay(buf->session(), relay_data::ssl_err);
		return;
	}
	_bufs.pop();
	if (_bufs.empty()) {
		return;
	}
	buf = _bufs.front();
	async_write(*_sock, buf->buffers(),
		    asio::bind_executor(
			    _strand,
			    std::bind(&ssl_relay::on_write_ssl, shared_from_this(),
				      std::placeholders::_1, std::placeholders::_2)));

}

// send data on ssl sock
// buf is read from sock in raw_relay
void ssl_relay::send_data_on_ssl(std::shared_ptr<relay_data> buf)
{
//	BOOST_LOG_TRIVIAL(info) << "send sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->data_size()<<"  on ssl ";
//	BOOST_LOG_TRIVIAL(info) << "head: "<< buf_to_string( buf->header_buffer().data(), buf->header_buffer().size());
//	BOOST_LOG_TRIVIAL(info) << " "<< buf_to_string( buf->data_buffer().data(), buf->data_buffer().size());
	if (_ssl_status == SSL_CLOSED) {
		return;
	}
	auto relay = _relays.find(buf->session());
	if (relay == _relays.end()) {
		return;
	}
	relay->second->timeout = TIMEOUT;

	_bufs.push(buf);
	if (_bufs.size() > 1) {
		return;
	}
	if (_ssl_status == NOT_START) {
		// start ssl connect
		_ssl_status = SSL_CONNECT;
		start_ssl_connect();
		return;
	}
	async_write(*_sock, buf->buffers(),
		    asio::bind_executor(
			    _strand,
			    std::bind(&ssl_relay::on_write_ssl, shared_from_this(),
				      std::placeholders::_1, std::placeholders::_2)));
//	BOOST_LOG_TRIVIAL(info) << "end of send sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->head()._len<<"  on ssl  ";
}

void ssl_relay::on_read_ssl_data(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error
	    || len != buf->data_size()) {
		BOOST_LOG_TRIVIAL(error) << "on read ssl data error: "<<error.message();
		BOOST_LOG_TRIVIAL(error) << "on read ssl data len "<< len <<" expect "<<buf->data_size();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
//	BOOST_LOG_TRIVIAL(info) << "read sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->head()._len<<"  on ssl ";
//	BOOST_LOG_TRIVIAL(info) << "on read ssl sess data:\n"<< buf_to_string( buf->data_buffer().data(), buf->data_buffer().size());
	auto session = buf->session();
	if ( buf->cmd() == relay_data::DATA_RELAY) {
		auto val = _relays.find(session);

		if (val == _relays.end()) {
			// session stopped, 
//			stop_ssl_relay(session, relay_data::from_raw);
		} else {
			val->second->timeout = TIMEOUT;
			auto relay = val->second->relay;
			auto raw_data_send = std::bind(&raw_relay::send_data_on_raw, relay, buf);
			relay->get_strand().post(raw_data_send, asio::get_associated_allocator(raw_data_send));
		}
	} else if (buf->cmd() == relay_data::START_CONNECT) {
		// remote get start connect
		auto relay = std::make_shared<raw_relay> (_io_context, shared_from_this(), session);
		_relays[session] = std::make_shared<ssl_relay::_relay_t>(relay);
//		relay->session(session);
//		BOOST_LOG_TRIVIAL(info) << " raw relay construct new session: "<<session;
		auto start_task = std::bind(&raw_relay::start_remote_connect, relay, buf);
		relay->get_strand().post(start_task, asio::get_associated_allocator(start_task));
	}

	start_ssl_data_relay();
}
void ssl_relay::on_read_ssl_header(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{

	if (error
	    || len != buf->header_buffer().size()
	    || buf->head()._len > READ_BUFFER_SIZE) {
		BOOST_LOG_TRIVIAL(error) << "on read ssl header error\n\t"<<error.message();
		BOOST_LOG_TRIVIAL(error) << "\theader len: "<<len << " expect "<<buf->header_size();
		BOOST_LOG_TRIVIAL(error) << "\tdata len: "<<buf->head()._len;
		BOOST_LOG_TRIVIAL(error) << "read sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->data_size()<<"  on ssl ";
		BOOST_LOG_TRIVIAL(error) << " "<< buf_to_string( buf->header_buffer().data(), buf->header_size());

		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	auto this_ptr = shared_from_this();
	auto session = buf->session();
//	BOOST_LOG_TRIVIAL(info) << "read sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->data_size()<<"  on ssl " << buf_to_string( buf->header_buffer().data(), buf->header_buffer().size());

	if (buf->data_size() != 0) {
		async_read(*_sock, buf->data_buffer(),
			   asio::bind_executor(
				   _strand,
				   [=](const boost::system::error_code& error, std::size_t len)
					   {
						   this_ptr->on_read_ssl_data(buf, error, len);
					   }));
//				   std::bind(&ssl_relay::on_read_ssl_data, shared_from_this(), buf,
//					     std::placeholders::_1, std::placeholders::_2)));
		return;
	}
// data size == 0
	if (buf->cmd() == relay_data::START_RELAY) {
//		BOOST_LOG_TRIVIAL(info) << session <<" START RELAY: ";
		auto val = _relays.find(session);
		if (val == _relays.end() ) {
			// local stopped before remote connect, tell remote to stop
			stop_ssl_relay(session, relay_data::from_raw);
		} else {
			// local get start from remote, tell raw relay begin
			auto relay = val->second->relay;
			auto start_task = std::bind(&raw_relay::start_data_relay, relay);
			relay->get_strand().post(start_task, asio::get_associated_allocator(start_task));
		}
	} else if (buf->cmd() == relay_data::STOP_RELAY) {
		// post stop to raw
		_relays.erase(session);
	}
	start_ssl_data_relay();
}

// start ssl data relay
void ssl_relay::start_ssl_data_relay()
{
	auto buf = std::make_shared<relay_data>(0);
	// BOOST_LOG_TRIVIAL(info) << "start header read: ";
	auto this_ptr = shared_from_this();
	_sock->async_read_some(
		buf->header_buffer(),
		asio::bind_executor(
			_strand,
			[=](const boost::system::error_code& error, std::size_t len)
				{
					this_ptr->on_read_ssl_header(buf, error, len);
				}));
//			std::bind(&ssl_relay::on_read_ssl_header, shared_from_this(), buf,
//				  std::placeholders::_1, std::placeholders::_2)));
//	BOOST_LOG_TRIVIAL(info) << "end of start header read: ";
}
// remote ssl relay server functions
void ssl_relay::on_ssl_handshake(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << "remote handshake error: "<<error.message();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	_ssl_status = SSL_START;
	start_ssl_data_relay();
}

void ssl_relay::remote_ssl_start()
{
	_sock->lowest_layer().set_option(tcp::no_delay(true));
	auto this_ptr = shared_from_this();
	_sock->async_handshake(ssl_socket::server,
			       [=](const boost::system::error_code& error) 
				       {
					       this_ptr->on_ssl_handshake(error);
				       });

			      // std::bind(&ssl_relay::on_ssl_handshake, shared_from_this(),
			      // 		std::placeholders::_1));

}
// local ssl relay server functions
// call add_new_relay, vector access, must run in ssl_relay strand
void ssl_relay::local_handle_accept(std::shared_ptr<raw_relay> relay)
{
	if (_ssl_status == SSL_CLOSED ) {
		BOOST_LOG_TRIVIAL(error) <<" SSL closed  ";
		return ;
	}

//	local_start_accept();
	add_new_relay(relay);

	auto task = std::bind(&raw_relay::local_start, relay);
	relay->get_strand().post(task, asio::get_associated_allocator(task));
}
// void ssl_relay::local_start_accept()
// {
// 	// accept raw relay
// 	auto relay = std::make_shared<raw_relay> (_io_context, shared_from_this());
// 	_acceptor.async_accept(relay->get_sock(),
// 			       asio::bind_executor(_strand,
// 						   std::bind(&ssl_relay::local_handle_accept,
// 							     shared_from_this(), relay, std::placeholders::_1)));
// }

uint32_t ssl_relay::add_new_relay(const std::shared_ptr<raw_relay> &relay)
{
	uint32_t session = 0;
	do {
		auto ran = _rand();
//		auto tmp = std::chrono::system_clock::now().time_since_epoch().count();
		auto tmp = time(nullptr);

		session = (ran & 0xffff0000) | (tmp & 0xffff);
//		BOOST_LOG_TRIVIAL(info) << " raw relay construct new session: "<<session;
	} while ( _relays.count(session) );

	relay->session(session);
	_relays.emplace(session, std::make_shared<_relay_t>(relay));
	return session;
}
void ssl_relay::local_ssl_handshake(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << "start ssl connect handshake error :" <<error.message();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	BOOST_LOG_TRIVIAL(info) << "ssl handshake ok" ;
	_ssl_status = SSL_START;
	auto buf = _bufs.front();
	async_write(*_sock, buf->buffers(),
		    asio::bind_executor(
			    _strand,
			    std::bind(&ssl_relay::on_write_ssl, shared_from_this(),
				      std::placeholders::_1, std::placeholders::_2)));

	start_ssl_data_relay();
}
void ssl_relay::on_ssl_connect(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << "start ssl connect error :" <<error.message();
		stop_ssl_relay(0, relay_data::ssl_err);
		return;
	}
	_sock->lowest_layer().set_option(tcp::no_delay(true));
	BOOST_LOG_TRIVIAL(info) << "start ssl handshake :" ;
	_sock->async_handshake(ssl_socket::client,
			      asio::bind_executor(_strand,
						  std::bind(&ssl_relay::local_ssl_handshake, shared_from_this(),
							    std::placeholders::_1)));


}
void ssl_relay::start_ssl_connect()
{
	BOOST_LOG_TRIVIAL(info) << "start ssl connect : ";
	_sock->lowest_layer().async_connect(
		_remote,
		asio::bind_executor(_strand,
				    std::bind(&ssl_relay::on_ssl_connect, shared_from_this(),
					      std::placeholders::_1)));

}

ssl::context init_ssl(const relay_config &config)
{
	ssl::context ctx(config.local ? 
			 ssl::context::tlsv12_client :
			 ssl::context::tlsv12_server);
	BOOST_LOG_TRIVIAL(info) << "cert : " << config.cert << " key:"<<config.key;
	ctx.load_verify_file(config.cert);//"yily.crt");
	ctx.set_verify_mode(ssl::verify_peer|ssl::verify_fail_if_no_peer_cert);
	ctx.use_certificate_file(config.cert/*"yily.crt"*/, ssl::context::pem);
	ctx.use_rsa_private_key_file(config.key/*"key.pem"*/, ssl::context::pem);
	return ctx;
}
