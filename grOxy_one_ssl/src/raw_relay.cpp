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

// common functions
void raw_relay::stop_raw_relay(const relay_data::stop_src src)
{
	if (_session == 0) {
		//already stopped
		return;
	}

	boost::system::error_code err;
	_sock.close(err);
	if (src == relay_data::from_raw) {
		auto task_ssl = std::bind(&ssl_relay::stop_ssl_relay, _manager, _session, src);
		_manager->get_strand().dispatch(task_ssl, asio::get_associated_allocator(task_ssl));
	}
	_session = 0;
}
void raw_relay::on_raw_send(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on raw send error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
}
void raw_relay::send_data_on_raw(std::shared_ptr<relay_data> buf)
{
	async_write(_sock, buf->data_buffer(),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::on_raw_send, shared_from_this(), buf,
						  std::placeholders::_1, std::placeholders::_2)));

}
void raw_relay::on_raw_read(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on raw read error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
//	BOOST_LOG_TRIVIAL(info) << " raw read len: "<< len;
	// dispatch to manager
	buf->resize(len);
	auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, _manager, buf);
	_manager->get_strand().dispatch(send_on_ssl, asio::get_associated_allocator(send_on_ssl));

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
		BOOST_LOG_TRIVIAL(info) << "on addr get error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}

}
void raw_relay::on_local_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error || len < 6 || (*buf)[1] != 1 ) {
		BOOST_LOG_TRIVIAL(info) << "on addr get error : "<<error.message();
		BOOST_LOG_TRIVIAL(info) << "\t len : "<<len << " cmd: "<< (*buf)[1];
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	int rlen = 6;
	switch (auto cmd = (*buf)[3]) {
	case 1:
		rlen += 4;
		break;
	case 3:
		rlen += 1+(*buf)[4];
		break;
	case 4:
		rlen += 16;
		break;
	default:
		rlen =0;
		BOOST_LOG_TRIVIAL(info) << "sock 5 cmd type not support" << cmd;
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	if (len != rlen) {
		BOOST_LOG_TRIVIAL(info) << "\t addr get  len : "<< len<< " rlen "<< rlen;
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	// send start cmd to ssl
	buf->resize(len);
	auto buffer = std::make_shared<relay_data>(_session, relay_data::START_RELAY);
//	buffer->data() = buf->substr(3);
	std::copy_n(&buf->data()[3], len -3, (uint8_t*)buffer->data_buffer().data());
//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
	buffer->resize(len -3);
	auto start_task = std::bind(&ssl_relay::start_new_relay, _manager, buffer);
	_manager->get_strand().dispatch(start_task, asio::get_associated_allocator(start_task));

	// send sock5 ok back
	// WIP write on start?
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
		BOOST_LOG_TRIVIAL(info) << "write 0x5 0x0 error: "<<error.message();
		BOOST_LOG_TRIVIAL(info) << "\twrite len: "<<len;
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
		BOOST_LOG_TRIVIAL(info) << "on start read error: "<<error.message();
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
		BOOST_LOG_TRIVIAL(info) << "sock5 addr4 len error: "<< error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	// send start relay
//	BOOST_LOG_TRIVIAL(info) << "on remote connect "<< error.message();
	auto buffer = std::make_shared<relay_data>(_session, relay_data::START_RELAY);
	auto start_task = std::bind(&ssl_relay::send_data_on_ssl, _manager, buffer);
	_manager->get_strand().dispatch(start_task, asio::get_associated_allocator(start_task));

	// start raw data relay
	start_data_relay();
}
void raw_relay::start_remote_connect(std::shared_ptr<relay_data> buf)
{
	auto data = (uint8_t*) buf->data_buffer().data();
	tcp::endpoint remote;
	uint8_t *port;
//	BOOST_LOG_TRIVIAL(info) << "start remote data: \n" << buf_to_string(buf->data_buffer().data(), buf->data_buffer().size());
	switch(auto cmd = data[0]) {
	case 1:{
		ip::address_v4::bytes_type *addr_4 = (ip::address_v4::bytes_type *)(&data[1]);
		if (buf->data_size() < sizeof(*addr_4) + 3) {
			BOOST_LOG_TRIVIAL(info) << "sock5 addr4 len error: ";
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		remote.address(ip::make_address_v4(*addr_4));
		port = (uint8_t*)&addr_4[1];
		remote.port(port[0]<<8 | port[1]);
		_sock.async_connect(remote,
				    asio::bind_executor(_strand,
							std::bind(&raw_relay::on_remote_connect, shared_from_this(),
								  std::placeholders::_1)));
		break;
	}
	case 4:{
		ip::address_v6::bytes_type *addr_6 = (ip::address_v6::bytes_type *)(&data[1]);
		if (buf->data_size() < sizeof(*addr_6) + 3) {
			BOOST_LOG_TRIVIAL(info) << "sock5 addr6 len error: ";
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		remote.address(ip::make_address_v6(*addr_6));
		port = (uint8_t*)&addr_6[1];
		remote.port(port[0]<<8 | port[1]);
		_sock.async_connect(remote,
				    asio::bind_executor(_strand,
							std::bind(&raw_relay::on_remote_connect, shared_from_this(),
								  std::placeholders::_1)));
		break;

	}
	case 3: {
		int host_len = data[1];
		if ( buf->data_size() < host_len + 1 +3) {
			BOOST_LOG_TRIVIAL(info) << "sock5 host name len error: ";
			stop_raw_relay(relay_data::from_raw);
			return;
		}
		std::string host_name((char*)&data[2], host_len);// = data.substr(2, host_len);

		port = &data[host_len+1 +1];

		std::ostringstream port_name;
		port_name << ((port[0]<<8)|port[1]);
		BOOST_LOG_TRIVIAL(debug) << "host " <<host_name <<" port "<<port_name.str() ;
		boost::system::error_code ec;
		auto re_hosts = _host_resolve.resolve(host_name, port_name.str(), ec);
		if (ec) {
			BOOST_LOG_TRIVIAL(debug) << "host resolve error"<<ec.message();
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
		BOOST_LOG_TRIVIAL(info) << "sock5 cmd type not support "<< cmd;
		stop_raw_relay(relay_data::from_raw);
		return;
	}

}
