#include "relay.hpp"
#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
std::string buf_to_string(void *buf, std::size_t size)
{
	std::ostringstream out;
	out << std::setfill('0') << std::setw(2) << std::hex;

	for (std::size_t i =0; i< size; i++ ) {
		unsigned int a = ((uint8_t*)buf)[i];
		out << a << ' ';
		if ((i+1) % 32 == 0) out << '\n';
	}
	return out.str();
}
static void parse_addr4(tcp::endpoint &remote, uint8_t* data, std::size_t len)
{
	ip::address_v4::bytes_type *addr_4 = (ip::address_v4::bytes_type *)data;
	if (len < sizeof(*addr_4) + 2) {
		auto emsg = boost::format(" sock5 addr4 len error: %1%")%len;
		throw_err_msg(emsg.str());
	}
	remote.address(ip::make_address_v4(*addr_4));
	auto port = (uint8_t*)&addr_4[1];
	remote.port(port[0]<<8 | port[1]);
}
static void parse_addr6(tcp::endpoint &remote, uint8_t* data, std::size_t len)
{
	ip::address_v6::bytes_type *addr_6 = (ip::address_v6::bytes_type *)data;
	if (len < sizeof(*addr_6) + 2) {
		auto emsg = boost::format(" sock5 addr6 len error: %1%")%len;
		throw_err_msg(emsg.str());
	}
	remote.address(ip::make_address_v6(*addr_6));
	auto port = (uint8_t*)&addr_6[1];
	remote.port(port[0]<<8 | port[1]);
}
static void parse_host(std::string &host, std::string &port, uint8_t* data, std::size_t len)
{
	int host_len = data[0];
	if ( len < host_len +3) {
		auto emsg = boost::format(" sock5 host name len error: %1%, hostlen%2%")%len%host_len;
		throw_err_msg(emsg.str());
	}
	host.append((char*)&data[1], host_len);
	auto pt = &data[host_len+1];

	std::ostringstream port_name;
	port_name << ((pt[0]<<8)|pt[1]);
	port = port_name.str();
	BOOST_LOG_TRIVIAL(debug) << "host " <<host <<" port "<<port ;
}

// common functions
void raw_relay::stop_raw_relay(const relay_data::stop_src src)
{
	if (_stopped) {
		//already stopped
		return;
	}
	_stopped = true;
//	BOOST_LOG_TRIVIAL(info) << " raw relay "<<_session <<" stopped: "<< "from "<< src<< _stopped;
	boost::system::error_code err;
	_sock.shutdown(tcp::socket::shutdown_both, err);
	_sock.close(err);
	if (src == relay_data::from_raw) {
		auto task_ssl = std::bind(&ssl_relay::stop_ssl_relay, _manager, _session, src);
		_manager->get_strand().post(task_ssl, asio::get_associated_allocator(task_ssl));
	}
}

void raw_relay::send_data_on_raw(std::shared_ptr<relay_data> buf)
{
	_bufs.push(buf);
	if (_bufs.size() > 1) {
		return;
	}
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self](asio::yield_context yield) {
		try {
			while (!_bufs.empty()) {
				auto buf = _bufs.front();
				auto len = async_write(_sock, buf->data_buffer(), yield);
				if (len != buf->data_size()) {
					auto emsg = boost::format(" wlen%1%, buflen%2%")%len%buf->data_size();
					throw_err_msg(emsg.str());
				}
				_bufs.pop();
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "raw write error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}

void raw_relay::start_data_relay()
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self](asio::yield_context yield) {
		try {
			while (true) {
				auto buf = std::make_shared<relay_data>(_session);
				auto len = _sock.async_read_some(buf->data_buffer(), yield);
				//	BOOST_LOG_TRIVIAL(info) << " raw read len: "<< len;
				// post to manager
				buf->resize(len);
				auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, _manager, buf);
				_manager->get_strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "raw read error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}

// local server functions
void raw_relay::local_relay(bool dir)
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self, dir](asio::yield_context yield) {
		try {
			auto sock_r = &_sock;
			auto sock_w = &_sock_remote;
			auto buf = &local_buf;
			if (dir) {
				sock_r = &_sock_remote;
				sock_w = &_sock;
				buf = &remote_buf;
			}
			while (true) {
				buf->resize(READ_BUFFER_SIZE);
				auto len = sock_r->async_read_some(asio::buffer(*buf), yield);
				buf->resize(len);
				len = async_write(*sock_w, asio::buffer(*buf), yield);
				if (len != buf->size()) {
					auto emsg = boost::format(" wlen%1%, buflen%2%")%len%buf->size();
					throw_err_msg(emsg.str());
				}
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "local relay error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}

void raw_relay::local_start()
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self](asio::yield_context yield) {
		try {
			auto buf = std::make_shared<std::string>(512,0);
			auto len = _sock.async_receive(asio::buffer(*buf), yield);

			(*buf)[0] = 5, (*buf)[1] = 0;
			len = async_write(_sock, asio::buffer(*buf, 2), yield);
			if (len != 2) {
				auto emsg = boost::format("write 0x5 0x0, len %1%")%len;
				throw_err_msg(emsg.str());
			}
// get sock5 connect cmd
			len = _sock.async_read_some(asio::buffer(*buf), yield);
			if (len < 6 || (*buf)[1] != 1 ) {
				auto emsg = boost::format("addr get len %1%, cmd %2%")%len %(int)(*buf)[1];
				throw_err_msg(emsg.str());
			}
			bool block = true;
			auto data = (uint8_t*) & (*buf)[3];
			auto cmd = data[0];
			tcp::endpoint remote;
			if (cmd == 1) {
				parse_addr4(remote, data+1, len -4);
			} else if (cmd == 4) {
				parse_addr6(remote, data+1, len -4);
			} else if (cmd == 3) {
				std::string host_name;
				std::string port_name;
				parse_host(host_name, port_name, data+1, len-4);
				block = _manager->check_host_gfw(host_name);
				//block = false;
				if (!block) {
// not block local sock
//			BOOST_LOG_TRIVIAL(debug) << "resolve host " <<host_name <<" port "<<port_name ;
					auto re_hosts = _host_resolve.async_resolve(host_name, port_name, yield);
//			BOOST_LOG_TRIVIAL(info) << "start remote connect : "<<ec.message();
					asio::async_connect(_sock_remote, re_hosts, yield);
					local_relay(true);
					local_relay(false);
					//						std::bind(&raw_relay::local_on_remote_connect, shared_from_this(),
				}

			} else {
				auto emsg = boost::format("sock5 cmd %1% not support")%cmd;
				throw(boost::system::system_error(
					      boost::system::error_code(),
					      emsg.str()));
			}
			if (block) {
				// send start cmd to ssl
				auto buffer = std::make_shared<relay_data>(_session, relay_data::START_CONNECT);
				std::copy_n(data, len -3, (uint8_t*)buffer->data_buffer().data());
//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
				buffer->resize(len -3);
				auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, _manager, buffer);
				_manager->get_strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
			}

			// send sock5 ok back
			// WIP write on start?
			//BOOST_LOG_TRIVIAL(info) << " send sock5 ok back : ";
			*buf =  {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
			async_write(_sock, asio::buffer(*buf), yield);

		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "local start error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}

void raw_relay::start_remote_connect(std::shared_ptr<relay_data> buf)
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self, buf](asio::yield_context yield) {
		try {
			auto data = (uint8_t*) buf->data_buffer().data();
			auto len = buf->data_size();
			tcp::endpoint remote;
			auto cmd = data[0];

//	BOOST_LOG_TRIVIAL(info) << "start remote data: \n" << buf_to_string(buf->data_buffer().data(), buf->data_buffer().size());
			if (cmd == 1) {
				parse_addr4(remote, data+1, len -1);
				_sock.async_connect(remote, yield);
			} else if (cmd == 4) {
				parse_addr6(remote, data+1, len -1);
				_sock.async_connect(remote, yield);
			} else if (cmd == 3) {
				std::string host_name;
				std::string port_name;
				parse_host(host_name, port_name, data+1, len-1);
				auto re_hosts = _host_resolve.async_resolve(host_name, port_name, yield);
				asio::async_connect(_sock, re_hosts, yield);
			} else {
				auto emsg = boost::format("sock5 cmd %1% not support")%cmd;
				throw_err_msg(emsg.str());
			}
			// on remote connect
			auto buffer = std::make_shared<relay_data>(_session, relay_data::START_RELAY);
			auto start_task = std::bind(&ssl_relay::send_data_on_ssl, _manager, buffer);
			_manager->get_strand().post(start_task, asio::get_associated_allocator(start_task));

			// start raw data relay
			start_data_relay();

		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "start remote error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}
