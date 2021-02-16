#include "relay.hpp"
#include <sstream>
#include <iomanip>
std::string buf_to_string(void *buf, std::size_t size)
{
	return "";

	std::ostringstream out;
	out << std::setfill('0') << std::setw(2) << std::hex;

//	out.setf(out.hex);
//	out.unsetf(out.showbase);
//	out.width(3);
	for (std::size_t i =0; i< size; i++ ) {
		unsigned int a = ((uint8_t*)buf)[i];
		out << a << ' ';
		if ((i+1) % 32 == 0) out << '\n';
	}
	return out.str();

}
static int parse_addr4(tcp::endpoint &remote, uint8_t* data, std::size_t len)
{
	ip::address_v4::bytes_type *addr_4 = (ip::address_v4::bytes_type *)data;
	if (len < sizeof(*addr_4) + 2) {
		BOOST_LOG_TRIVIAL(error) << "sock5 addr4 len error: "<< len;
		return -1;
	}
	remote.address(ip::make_address_v4(*addr_4));
	auto port = (uint8_t*)&addr_4[1];
	remote.port(port[0]<<8 | port[1]);
	return 0;

}
static int parse_addr6(tcp::endpoint &remote, uint8_t* data, std::size_t len)
{
	ip::address_v6::bytes_type *addr_6 = (ip::address_v6::bytes_type *)data;
	if (len < sizeof(*addr_6) + 2) {
		BOOST_LOG_TRIVIAL(error) << "sock5 addr6 len error: "<< len;
		return -1;
	}
	remote.address(ip::make_address_v6(*addr_6));
	auto port = (uint8_t*)&addr_6[1];
	remote.port(port[0]<<8 | port[1]);
	return 0;
}
static int parse_host(std::string &host, std::string &port, uint8_t* data, std::size_t len)
{
	int host_len = data[0];
	if ( len < host_len +3) {
		BOOST_LOG_TRIVIAL(error) << "sock5 host name len error: " << len <<"host len" << host_len;
		return -1;
	}
	host.append((char*)&data[1], host_len);
	auto pt = &data[host_len+1];

	std::ostringstream port_name;
	port_name << ((pt[0]<<8)|pt[1]);
	port = port_name.str();
	BOOST_LOG_TRIVIAL(debug) << "host " <<host <<" port "<<port ;

	return 0;

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
void raw_relay::on_raw_send(/*std::shared_ptr<relay_data> buf, */const boost::system::error_code& error, std::size_t len)
{
	auto buf = _bufs.front();
	if (error
	    || len != buf->data_size()) {
		BOOST_LOG_TRIVIAL(error) << "on raw relay "<<_session<<"send error: "<<error.message();
		BOOST_LOG_TRIVIAL(error) << "\tlen: "<<len << " data size "<<buf->data_size();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	_bufs.pop();
	if (_bufs.empty()) {
		return;
	}
	async_write(_sock, _bufs.front()->data_buffer(),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::on_raw_send, shared_from_this(),
						  std::placeholders::_1, std::placeholders::_2)));

}
void raw_relay::send_data_on_raw(std::shared_ptr<relay_data> buf)
{
	_bufs.push(buf);
	if (_bufs.size() > 1) {
		return;
	}

	async_write(_sock, buf->data_buffer(),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::on_raw_send, shared_from_this(),
						  std::placeholders::_1, std::placeholders::_2)));

}
void raw_relay::on_raw_read(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << "on raw relay "<<_session<<" read error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
//	BOOST_LOG_TRIVIAL(info) << " raw read len: "<< len;
	// post to manager
	buf->resize(len);
	auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, _manager, buf);
	_manager->get_strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));

	start_data_relay();
}
void raw_relay::start_data_relay()
{
	// new buf for read
	auto buf = std::make_shared<relay_data>(_session);
	_sock.async_read_some(buf->data_buffer(),
			      asio::bind_executor(_strand,
						  std::bind(&raw_relay::on_raw_read, shared_from_this(), buf,
							    std::placeholders::_1, std::placeholders::_2)));

}

// local server functions
void raw_relay::on_local_addr_ok(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << "on addr get error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}

}
void raw_relay::on_nogfw_write(tcp::socket &sock_r, tcp::socket &sock_w, std::string &buf, const boost::system::error_code& error, std::size_t len)
{
	if (error || len != buf.size()) {
		BOOST_LOG_TRIVIAL(error) << " on noblock write error: "<<error.message();
		BOOST_LOG_TRIVIAL(error) << "  len: "<<len << " buf len" <<buf.size();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	nogfw_read(sock_r, sock_w, buf);
}
void raw_relay::on_nogfw_read(tcp::socket &sock_r, tcp::socket &sock_w, std::string &buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << " on noblock read error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	buf.resize(len);
	async_write(sock_w, asio::buffer(buf),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::on_nogfw_write, shared_from_this(),
						  std::ref(sock_r), std::ref(sock_w), std::ref(buf),
						  std::placeholders::_1, std::placeholders::_2)));
}
void raw_relay::nogfw_read(tcp::socket &sock_r, tcp::socket &sock_w, std::string &buf)
{
	buf.resize(READ_BUFFER_SIZE);
	sock_r.async_read_some(asio::buffer(buf),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::on_nogfw_read, shared_from_this(),
						  std::ref(sock_r), std::ref(sock_w), std::ref(buf),
						  std::placeholders::_1, std::placeholders::_2)));
}
void raw_relay::local_on_remote_connect(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << " connect remote error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
//	BOOST_LOG_TRIVIAL(info) << " on connect remote "<<error.message();
	nogfw_read(_sock, _sock_remote, local_buf);
	nogfw_read(_sock_remote, _sock, remote_buf);
}
void raw_relay::on_local_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len)
{
	bool block = true;
	auto data = (uint8_t*) & (*buf)[3];
	if (error || len < 6 || (*buf)[1] != 1 ) {
		BOOST_LOG_TRIVIAL(error) << "on addr get error : "<<error.message();
		BOOST_LOG_TRIVIAL(error) << "\t len : "<<len << " cmd: "<< (int)(*buf)[1];
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	switch (auto cmd = data[0]) {
	case 1: {
		tcp::endpoint remote;
		if (parse_addr4(remote, data+1, len -4)) {
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		// _sock.async_connect(remote,
		//		    asio::bind_executor(_strand,
		//					std::bind(&raw_relay::on_remote_connect, shared_from_this(),
		//						  std::placeholders::_1)));
		break;
	}
	case 3: {
		std::string host_name;
		std::string port_name;

		if (parse_host(host_name, port_name, data+1, len-4)) {
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		block = _manager->gfw.is_blocked(host_name);
		//block = false;

		if (!block) {
//			BOOST_LOG_TRIVIAL(debug) << "resolve host " <<host_name <<" port "<<port_name ;
			boost::system::error_code ec;
			auto re_hosts = _host_resolve.resolve(host_name, port_name, ec);
			if (ec) {
				BOOST_LOG_TRIVIAL(debug) << "host resolve error"<<ec.message();
				stop_raw_relay(relay_data::from_raw);
				return;

			}
//			BOOST_LOG_TRIVIAL(info) << "start remote connect : "<<ec.message();
			asio::async_connect(_sock_remote, re_hosts,
					    asio::bind_executor(_strand,
								std::bind(&raw_relay::local_on_remote_connect, shared_from_this(),
									  std::placeholders::_1)));

		}
		break;
	}
	case 4: {
		tcp::endpoint remote;
		if (parse_addr6(remote, data+1, len -4)) {
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		// _sock.async_connect(remote,
		//		    asio::bind_executor(_strand,
		//					std::bind(&raw_relay::on_remote_connect, shared_from_this(),
		//						  std::placeholders::_1)));
		break;
	}
	default:
		BOOST_LOG_TRIVIAL(info) << "sock 5 cmd type not support" << cmd;
		stop_raw_relay(relay_data::from_raw);
		return;
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
	async_write(_sock, asio::buffer(*buf),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::on_local_addr_ok, shared_from_this(), buf,
						  std::placeholders::_1, std::placeholders::_2)));

}
// get sock5 connect cmd
void raw_relay::start_local_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error || len != 2) {
		BOOST_LOG_TRIVIAL(error) << "write 0x5 0x0 error: "<<error.message();
		BOOST_LOG_TRIVIAL(error) << "\twrite len: "<<len;
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	_sock.async_read_some(asio::buffer(*buf),
			     asio::bind_executor(_strand,
						 std::bind(&raw_relay::on_local_addr_get, shared_from_this(), buf,
								       std::placeholders::_1, std::placeholders::_2)));
}

void raw_relay::local_on_start(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << "on start read error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}

	(*buf)[0] = 5, (*buf)[1] = 0;
	async_write(_sock, asio::buffer(*buf, 2),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::start_local_addr_get, shared_from_this(), buf,
						  std::placeholders::_1, std::placeholders::_2)));

}
void raw_relay::local_start()
{
	auto buf = std::make_shared<std::string>(512,0);
	_sock.async_receive(asio::buffer(*buf),
			    asio::bind_executor(_strand,
						std::bind(&raw_relay::local_on_start, shared_from_this(), buf,
							  std::placeholders::_1, std::placeholders::_2)));
}

// remote server functions
void raw_relay::on_remote_connect(const boost::system::error_code& error)
{
	if (error) {
		BOOST_LOG_TRIVIAL(error) << " raw relay "<<_session<<" remote connect error: "<< error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	// send start relay
//	BOOST_LOG_TRIVIAL(info) << _session << " on remote connect, start raw data relay "<< error.message();
	auto buffer = std::make_shared<relay_data>(_session, relay_data::START_RELAY);
	auto start_task = std::bind(&ssl_relay::send_data_on_ssl, _manager, buffer);
	_manager->get_strand().post(start_task, asio::get_associated_allocator(start_task));

	// start raw data relay
	start_data_relay();
}

// remote raw_relay: connect to remote site
void raw_relay::start_remote_connect(std::shared_ptr<relay_data> buf)
{
	auto data = (uint8_t*) buf->data_buffer().data();
	auto len = buf->data_size();
	tcp::endpoint remote;
//	BOOST_LOG_TRIVIAL(info) << "start remote data: \n" << buf_to_string(buf->data_buffer().data(), buf->data_buffer().size());
	switch(auto cmd = data[0]) {
	case 1:{
		if (parse_addr4(remote, data+1, len -1)) {
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		_sock.async_connect(remote,
				    asio::bind_executor(_strand,
							std::bind(&raw_relay::on_remote_connect, shared_from_this(),
								  std::placeholders::_1)));
		break;
	}
	case 4:{
		if (parse_addr6(remote, data+1, len -1)) {
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		_sock.async_connect(remote,
				    asio::bind_executor(_strand,
							std::bind(&raw_relay::on_remote_connect, shared_from_this(),
								  std::placeholders::_1)));
		break;

	}
	case 3: {
		std::string host_name;
		std::string port_name;

		if (parse_host(host_name, port_name, data+1, len-1)) {
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		boost::system::error_code ec;
		auto re_hosts = _host_resolve.resolve(host_name, port_name, ec);
		if (ec) {
			BOOST_LOG_TRIVIAL(error) << "host resolve error"<<ec.message();
			stop_raw_relay(relay_data::from_raw);
			return;

		}
		asio::async_connect(_sock, re_hosts,
				    asio::bind_executor(_strand,
							std::bind(&raw_relay::on_remote_connect, shared_from_this(),
								  std::placeholders::_1)));
		break;
	}

	default:
		BOOST_LOG_TRIVIAL(error) << "sock5 cmd type not support "<< cmd;
		stop_raw_relay(relay_data::from_raw);
		return;
	}

}
