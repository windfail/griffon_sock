#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include <vector>
#include <thread>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace logging = boost::log;
namespace keywords = boost::log::keywords;

namespace asio = boost::asio;
namespace ip = boost::asio::ip;
using boost::asio::ip::tcp;

const int READ_BUFFER_SIZE = 4096;

class tcp_relay
	:public std::enable_shared_from_this<tcp_relay>
{
public:
	tcp_relay(asio::io_context &io) :
	       	_strand(io), _local_sock(io), _remote_sock(io)
		{
			_local_data.in.resize(READ_BUFFER_SIZE,0);
			_remote_data.in.resize(READ_BUFFER_SIZE, 0);
		}
	~tcp_relay()  {BOOST_LOG_TRIVIAL(debug) << "destruct relay object";}
	void start_relay();
	tcp::socket & get_local_sock() {return _local_sock;}
	tcp::socket & get_remote_sock() {return _remote_sock;}
private:
	typedef struct { std::string in; std::string out; } data_t;

	asio::io_context::strand _strand;
	tcp::socket _local_sock;
	tcp::socket _remote_sock;
	data_t _local_data;
	data_t _remote_data;
	void stop_relay();
	void read_data(tcp::socket &sock_r, tcp::socket &sock_w, data_t &data,
		       const boost::system::error_code &error, std::size_t len);
	void send_data(tcp::socket &sock_r, tcp::socket &sock_w, data_t &data,
		       const boost::system::error_code& error, std::size_t len);

};

void tcp_relay::start_relay()
{
	boost::system::error_code init_err;

	read_data(_local_sock, _remote_sock, _local_data, init_err , 0);
	read_data(_remote_sock, _local_sock, _remote_data, init_err, 0);

}
void tcp_relay::stop_relay()
{
	boost::system::error_code err;
	_local_sock.close(err);
	_remote_sock.close(err);
}
void tcp_relay::read_data(tcp::socket &sock_r, tcp::socket &sock_w, data_t &data,
		const boost::system::error_code &error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on write error: "<<error.message()<<std::endl;
		stop_relay();
		return;
	}
	if (len != data.out.size()) {
		BOOST_LOG_TRIVIAL(info) << "send len "<<len <<" not match buf len "<<data.out.size()<<std::endl;
		stop_relay();
		return;
	}
	sock_r.async_receive(asio::buffer(data.in),
			     asio::bind_executor(_strand,
						 std::bind(&tcp_relay::send_data,
							   shared_from_this(),
							   std::ref(sock_r), std::ref(sock_w), std::ref(data),
							   std::placeholders::_1, std::placeholders::_2)));
}
void tcp_relay::send_data(tcp::socket &sock_r, tcp::socket &sock_w, data_t &data,
			   const boost::system::error_code& error, std::size_t len)
{
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "on read error: "<<error.message()<<std::endl;
		stop_relay();
		return;
	}
	if (len == 0) {
		data.out.clear();
		boost::system::error_code init_err;
		read_data(sock_r, sock_w, data, init_err, 0);
		return;
	}
	data.out = data.in.substr(0, len);
	async_write(sock_w, asio::buffer(data.out),
		    asio::bind_executor(_strand,
					std::bind(&tcp_relay::read_data,
						  shared_from_this(),
						  std::ref(sock_r), std::ref(sock_w), std::ref(data),
						  std::placeholders::_1, std::placeholders::_2)));
}

class tcp_server
{
public:
	tcp_server(int port) : _io_context(), _acceptor(_io_context, tcp::endpoint(tcp::v4(), port)) {}
	void start_accept();
	void handle_accept(std::shared_ptr<tcp_relay> sock_ptr, const boost::system::error_code& error);
	void run() { _io_context.run(); }

private:
	asio::io_context _io_context;
	tcp::acceptor _acceptor;
};

static int get_port(tcp::socket &local_sock)
{
	std::array<uint8_t, 2> sock_data;
	local_sock.receive(asio::buffer(sock_data));
	return (sock_data[0]<<8) | sock_data[1];
}

static int init_remote(asio::io_context &io, tcp::socket &local_sock, tcp::socket &remote_sock)
{
	std::string _data_r;
	_data_r.resize(512,0);
	local_sock.receive(asio::buffer(_data_r));

	uint8_t sock_ok[] = {5,0};
	local_sock.send(asio::buffer(sock_ok));

	int len = local_sock.receive(asio::buffer(_data_r, 4));
	if (len != 4 ||_data_r[1] != 1) {	// not connect cmd, error
		throw(std::runtime_error("sock5 receive connect command fail"));
	}
	tcp::endpoint remote;
	auto type = _data_r[3];
	std::string host_name;

	switch (type) {
	case 1:{
		ip::address_v4::bytes_type addr_4;
		local_sock.receive(asio::buffer(addr_4));
		remote.address(ip::make_address_v4(addr_4));
		remote.port(get_port(local_sock));
		remote_sock.connect(remote);
		break;
	}
	case 4:{
		ip::address_v6::bytes_type addr_6;
		local_sock.receive(asio::buffer(addr_6));
		remote.address(ip::make_address_v6(addr_6));
		remote.port(get_port(local_sock));
		remote_sock.connect(remote);
		break;
	}
	case 3:{
		local_sock.receive(asio::buffer(_data_r,1));
		len = _data_r[0];
		if (len != local_sock.receive(asio::buffer(_data_r,len))) {
			throw(std::runtime_error("sock5 receive host name fail"));
		}
		host_name = _data_r.substr(0, len);
		tcp::resolver host_resolve(io);
		std::ostringstream port_name;
		port_name << get_port(local_sock);
//		BOOST_LOG_TRIVIAL(debug) << "host " <<host_name <<" port "<<port_name.str() <<std::endl;
		auto re_hosts = host_resolve.resolve(host_name, port_name.str());
		connect(remote_sock, re_hosts);

		break;
	}
	default:
		throw(std::runtime_error("sock5 cmd not support"));

	}
	return 0;
}

void tcp_server::handle_accept( std::shared_ptr<tcp_relay> sock_ptr, const boost::system::error_code& error)
{
	start_accept();
	if (error) {
		return;
	}
	uint8_t ret_val[] =  {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
	try {
		init_remote(_io_context, sock_ptr->get_local_sock(), sock_ptr->get_remote_sock());
	} catch(std::runtime_error& error) {
		BOOST_LOG_TRIVIAL(info) <<"prepare throw exception: " << error.what()<<std::endl;
		ret_val[1] = 1;
	}

	boost::system::error_code err;
	write(sock_ptr->get_local_sock(), asio::buffer(ret_val), err);
	if (err) {
		return;
	}
	sock_ptr->start_relay();
}

void tcp_server::start_accept()
{
	auto sock_ptr = std::make_shared<tcp_relay> (_io_context);

	_acceptor.async_accept(sock_ptr->get_local_sock(),
			       std::bind(&tcp_server::handle_accept, this, sock_ptr, std::placeholders::_1));
}

static void init_log()
{
	logging::add_file_log(keywords::file_name = "sample.log",
			      keywords::target_file_name = "sample.log",
			      keywords::format = "[%ThreadID%][%TimeStamp%]: %Message%"                                 /*< log record format >*/);

	logging::add_common_attributes();
	// logging::core::get()->set_filter (
	// 	logging::trivial::severity >= logging::trivial::info
	// 	);
	// BOOST_LOG_TRIVIAL(trace) << "A trace severity message";
	// BOOST_LOG_TRIVIAL(debug) << "A debug severity message";
	BOOST_LOG_TRIVIAL(info) << "An informational severity message";
	BOOST_LOG_TRIVIAL(warning) << "A warning severity message";
	BOOST_LOG_TRIVIAL(error) << "An error severity message";
	BOOST_LOG_TRIVIAL(fatal) << "A fatal severity message";
}

int server(int port, int thread_num)
{
	init_log();

	tcp_server server(port);
	std::vector<std::thread> server_th;

	server.start_accept();
	for (int i = 1; i < thread_num; i++) {
		server_th.emplace_back([&](){ server.run();});
	}
	server.run();
	return 0;
}

