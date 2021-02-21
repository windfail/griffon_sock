#include "relay_server.hpp"

void relay_server::local_handle_accept(std::shared_ptr<ssl_relay> ssl_ptr, std::shared_ptr<raw_relay> new_relay, const boost::system::error_code& error)
{
	local_server_start(ssl_ptr);
	if (error) {
		BOOST_LOG_TRIVIAL(error) <<" relay_server handle accept error ";
		return;
	}

	auto task = std::bind(&ssl_relay::local_handle_accept, ssl_ptr, new_relay);
	ssl_ptr->get_strand().post(task, asio::get_associated_allocator(task));
}
void relay_server::local_server_start(std::shared_ptr<ssl_relay> ssl_ptr)
{
	auto new_relay = std::make_shared<raw_relay> (&_io_context, ssl_ptr);
	_acceptor.async_accept(new_relay->get_sock(),
			       std::bind(&relay_server::local_handle_accept, this, ssl_ptr, new_relay, std::placeholders::_1));

//	_timer.expires_after(std::chrono::minutes(1));
//	_timer.async_wait(std::bind(&relay_server::handle_timer, this, std::placeholders::_1));

}

void relay_server::handle_timer(const boost::system::error_code& err)
{
//	auto ssl_timer = std::bind(&ssl_relay::timer_handle, _ssl_server);
//	_ssl_server->get_strand().post(ssl_timer, asio::get_associated_allocator(ssl_timer));

//	_timer.expires_after(std::chrono::minutes(1));
//	_timer.async_wait(std::bind(&relay_server::handle_timer, this, std::placeholders::_1));
}

void relay_server::remote_handle_accept(std::shared_ptr<ssl_relay> ssl_ptr, const boost::system::error_code& error)
{
	auto new_ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
	remote_server_start(new_ssl_ptr);
	if (error) {
		BOOST_LOG_TRIVIAL(error) << "accept ssl connect error: "<<error.message();
		return;
	}

	ssl_ptr->ssl_connect_start();
}
void relay_server::remote_server_start(std::shared_ptr<ssl_relay> ssl_ptr)
{
	_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(),
			       std::bind(&relay_server::remote_handle_accept, this, ssl_ptr, std::placeholders::_1));
}
void relay_server::start_server()
{
	auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
	if (_config.local) {
		local_server_start(ssl_ptr);
	} else {
		remote_server_start(ssl_ptr);
	}
}
void relay_server::run()
{
	try {
		_io_context.run();
	} catch (std::exception & e) {
		BOOST_LOG_TRIVIAL(error) << "server run error: "<<e.what();
	} catch (...) {
		BOOST_LOG_TRIVIAL(error) << "server run error with unkown exception ";
	}
}
