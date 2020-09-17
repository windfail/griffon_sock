#include "relay.hpp"

void raw_relay::stop_raw_relay(const relay_data::stop_src src)
{
	boost::system::error_code err;
	_sock.close(err);
	if (src == relay_data::from_ssl) {
		return;
	}
	auto task_ssl = std::bind(&ssl_relay::stop_ssl_relay, _manager, _index, relay_data::from_raw);
	_manager->get_strand().dispatch(task_ssl, asio::get_associated_allocator(task_ssl));
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

	// dispatch to manager
	buf->resize(len);
	auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, _manager, buf);
	_manager->get_strand().dispatch(send_on_ssl, asio::get_associated_allocator(send_on_ssl));

	boost::system::error_code err;
	start_data_relay();
}
void raw_relay::start_data_relay()
{
	// new buf for read
	auto buf = std::make_shared<relay_data>(_index, relay_data::DATA_RELAY);
	_sock.async_read_some(buf->data_buffer(),
			      asio::bind_executor(_strand,
						  std::bind(&raw_relay::on_raw_read, shared_from_this(), buf,
							    std::placeholders::_1, std::placeholders::_2)));

}

void raw_relay::on_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on addr get error: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}

}
// get sock5 connect cmd
void raw_relay::start_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len)
{
	if (error || len < 6 || (*buf)[1] != 1 ) {
		BOOST_LOG_TRIVIAL(info) << "start addr get error or len less than 6: "<<error.message();
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	int rlen = 6;
	switch ((*buf)[3]) {
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
		break;
	}
	if (len != rlen) {
		BOOST_LOG_TRIVIAL(info) << "start addr get  len : "<< len<< " rlen "<< rlen;
		stop_raw_relay(relay_data::from_raw);
		return;
	}
	// send start cmd to ssl
	auto buffer = std::make_shared<relay_data>(_index, relay_data::START_RELAY);
	buffer->data() = buf->substr(3);
	buffer->resize(buffer->data().size());
	auto start_task = std::bind(&ssl_relay::send_data_on_ssl, _manager, buffer);
	_manager->get_strand().dispatch(start_task, asio::get_associated_allocator(start_task));

	// send sock5 ok back
	// WIP write on start?
	*buf =  {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
	async_write(_sock, asio::buffer(*buf),
		    asio::bind_executor(_strand,
					std::bind(&raw_relay::on_addr_get, shared_from_this(), buf,
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
					std::bind(&raw_relay::start_addr_get, shared_from_this(), buf,
						  std::placeholders::_1, std::placeholders::_2)));

}
void raw_relay::local_start()
{
//	auto buf = std::make_shared<relay_data>(_index, relay_data::DATA_RELAY);
	auto buf = std::make_shared<std::string>(512,0);
	_sock.async_receive(asio::buffer(*buf),
			    asio::bind_executor(_strand,
						std::bind(&raw_relay::local_on_start, shared_from_this(), buf,
							  std::placeholders::_1, std::placeholders::_2)));
}


void raw_relay::start_remote_connect(std::shared_ptr<relay_data> buf)
{

}
