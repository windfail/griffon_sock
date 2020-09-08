#include "relay.hpp"

#include <iostream>
#include <vector>
#include <thread>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace logging = boost::log;
namespace keywords = boost::log::keywords;



void ssl_relay::start_relay()
{
	boost::system::error_code init_err;

	read_data(_raw_sock, _ssl_sock, _raw_data, init_err , 0);
	read_data(_ssl_sock, _raw_sock, _ssl_data, init_err, 0);

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
		BOOST_LOG_TRIVIAL(info) << "on write error: "<<error.message()<<std::endl;
		stop_relay();
		return;
	}
	if (len != data.size()) {
		BOOST_LOG_TRIVIAL(info) << "send len "<<len <<" not match buf len "<<data.size()<<std::endl;
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
		BOOST_LOG_TRIVIAL(info) << "on read error: "<<error.message()<<std::endl;
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
