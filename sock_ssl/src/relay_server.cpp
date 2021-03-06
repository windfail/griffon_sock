#include "relay_server.hpp"
#include <boost/asio/spawn.hpp>

void relay_server::local_server_start()
{
	asio::spawn([this](asio::yield_context yield) {
		auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
		while (true) {
			try {
				auto new_relay = std::make_shared<raw_relay> (&_io_context, ssl_ptr);
				_acceptor.async_accept(new_relay->get_sock(), yield);
				auto task = std::bind(&ssl_relay::local_handle_accept, ssl_ptr, new_relay);
				ssl_ptr->get_strand().post(task, asio::get_associated_allocator(task));
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
			}
		}
	});

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
void relay_server::remote_server_start()
{
	asio::spawn([this](asio::yield_context yield) {
		while (true) {
			try {
				auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
				_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(), yield);
//						       std::bind(&relay_server::remote_handle_accept, this, ssl_ptr, std::placeholders::_1));
				auto task = std::bind(&ssl_relay::ssl_connect_start, ssl_ptr);
				ssl_ptr->get_strand().post(task, asio::get_associated_allocator(task));

			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "remote accept error: "<<error.what();
			}
		}
	});

}
void relay_server::start_server()
{
	if (_config.local) {
		local_server_start();
	} else {
		remote_server_start();
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
