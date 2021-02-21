#ifndef _GROXY_RELAY_HPP
#define _GROXY_RELAY_HPP
#include <random>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/coroutine.hpp>
#include <memory>
#include <unordered_map>
#include <queue>
#include "gfwlist.hpp"
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace logging = boost::log;
namespace keywords = boost::log::keywords;

namespace asio = boost::asio;
namespace ip = boost::asio::ip;
using boost::asio::ip::tcp;

namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

const int READ_BUFFER_SIZE = 4096;
const int TIMEOUT =  5;

struct relay_config
{
	int local_port = 10230;
	int remote_port = 10230;
	std::string remote_ip = "";
	int thread_num = 1;
	bool local = true;
	std::string cert = "/etc/groxy_ssl/groxy_ssl.crt";
	std::string key = "/etc/groxy_ssl/groxy_ssl.pem";
	std::string logfile = "/dev/null";
	std::string gfw_file = "/etc/groxy_ssl/gfwlist";

};

ssl::context init_ssl(const relay_config &config);
class ssl_relay;

class relay_data
{

public:
	enum command {
		STOP_RELAY,
		START_CONNECT,
		START_RELAY,
		DATA_RELAY
	};
	enum stop_src {
		from_ssl,
		from_raw,
		ssl_err
	};

	struct _header_t {
		uint32_t _session;
		command _cmd;
		std::size_t _len;
		_header_t(uint32_t session, command cmd, std::size_t len) :_session(session), _cmd(cmd), _len(len) {
		}
	};
private:
	_header_t _header;
	uint8_t _data[READ_BUFFER_SIZE];

public:
	relay_data(uint32_t session) :_header(session, DATA_RELAY, READ_BUFFER_SIZE) {
	}

	relay_data(uint32_t session, command cmd) : _header(session, cmd, 0) {

	}

	_header_t & head() {
		return _header;
	}
	auto session() {
//		auto hd = (_header_t*)&_data[0];
		return _header._session;
	}
	auto cmd() {
//		auto hd = (_header_t*)&_data[0];
		return _header._cmd;
	}

	auto header_buffer() {
		return asio::buffer(&_header, sizeof(_header_t));
	}
	auto data_buffer() {
		//return asio::buffer(&_data[sizeof(_header_t)], _header._len);
		return asio::buffer(_data, _header._len);
	}
	auto buffers() {
		//return asio::buffer(&_header, sizeof(_header_t)+_header._len);
		return std::array<asio::mutable_buffer, 2> { header_buffer(), data_buffer() };
//asio::buffer(&_header, sizeof(_header_t)), asio::buffer(_data)} ;
	}
	void resize(std::size_t data_len) {
		_header._len = data_len;
//		_data.resize(data_len);
	}
	auto header_size() {
		return sizeof(_header_t);
	}
	auto data_size() {
		return _header._len;
	}
	auto size() {
		return _header._len + sizeof(_header_t);
	}
private:


};

// class base_relay
//	:public std::enable_shared_from_this<base_relay>
// {

// }
// raw relay , for client to local server and remote server to dest
class raw_relay
	:public std::enable_shared_from_this<raw_relay>
{
public:
	raw_relay(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session = 0) :
		_session (session), _strand(*io), _sock(*io), _host_resolve(*io), _manager(manager), _sock_remote(*io)
		{
			BOOST_LOG_TRIVIAL(info) << "raw relay construct: ";
		}
	~raw_relay() {
		BOOST_LOG_TRIVIAL(info) << "raw relay destruct: "<<_session;
	}
	void local_start();

	tcp::socket & get_sock() {return _sock;}
	auto session() {return _session;}
	void session(uint32_t id) { _session = id;}
	void stop_raw_relay(relay_data::stop_src);
	asio::io_context::strand & get_strand() {
		return _strand;
	}
	void send_data_on_raw(std::shared_ptr<relay_data> buf);
	void start_data_relay();

	void start_remote_connect(std::shared_ptr<relay_data> buf);

private:
	asio::io_context::strand _strand;

	uint32_t _session;

	tcp::socket _sock;
	tcp::resolver _host_resolve;
	std::shared_ptr<ssl_relay> _manager;
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
	bool _stopped = false;
	tcp::socket _sock_remote;
	std::string local_buf;
	std::string remote_buf;
	void on_raw_send(/*std::shared_ptr<relay_data> buf, */const boost::system::error_code& error, std::size_t len);

	void on_raw_read(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len);

	void on_local_addr_ok(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);
	void on_local_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);
	void start_local_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);
	void local_on_start(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);

	void on_remote_connect(const boost::system::error_code& error);

	void local_on_remote_connect(const boost::system::error_code& error);
	void nogfw_read(tcp::socket &sock_r, tcp::socket &sock_w, std::string &buf);
	void on_nogfw_read(tcp::socket &sock_r, tcp::socket &sock_w, std::string &buf, const boost::system::error_code& error, std::size_t len);
	void on_nogfw_write(tcp::socket &sock_r, tcp::socket &sock_w, std::string &buf, const boost::system::error_code& error, std::size_t len);

};

// ssl relay , maintain tls between local server and remote server
class ssl_relay
	:public std::enable_shared_from_this<ssl_relay>
{
public:
	ssl_relay(asio::io_context *io, const relay_config &config) :
		_io_context(io), _strand(io->get_executor()), _ctx(init_ssl(config)),
		_sock(std::make_unique<ssl_socket>(*io, _ctx)),
//		_acceptor(*io()),//, tcp::endpoint(tcp::v4(), config.local_port)),
		_remote(ip::make_address(config.remote_ip), config.remote_port),
		_timer(*io), _rand(std::random_device()()),
		_config(config), _gfw(config.gfw_file)
	{

		BOOST_LOG_TRIVIAL(debug) << "ssl relay construct";
	}

	~ssl_relay()  {
		BOOST_LOG_TRIVIAL(debug) << "ssl relay destruct";
	};

	auto & get_strand() {
		return _strand;
	}
	void stop_ssl_relay(uint32_t session, relay_data::stop_src src);
	void send_data_on_ssl(std::shared_ptr<relay_data> buf);

	ssl_socket & get_sock() {return *_sock;}

	void ssl_connect_start();
	void local_handle_accept(std::shared_ptr<raw_relay> relay);

	void timer_handle();
	bool check_host_gfw(const std::string &host)
		{
			return _gfw.is_blocked(host);
		}

private:
	class _relay_t;
	asio::io_context *_io_context;
	enum {
		NOT_START,
		SSL_CONNECT,
		SSL_START,
		SSL_CLOSED
	} _ssl_status = NOT_START;
	relay_config _config;

	ssl::context _ctx;
	asio::strand<asio::io_context::executor_type> _strand;

	std::shared_ptr<ssl_socket>  _sock;

	std::unordered_map<uint32_t, std::shared_ptr<_relay_t>> _relays;
	asio::steady_timer _timer;

	tcp::endpoint _remote;	// remote ssl relay ep
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
	gfw_list _gfw;

	// random
	std::minstd_rand _rand;
	coroutine _start_ssl;
	coroutine _ssl_read;
	coroutine _ssl_write;
//	static const boost::system::error_code _error;

//	void on_read_ssl_header(std::shared_ptr<relay_data> w_data, const boost::system::error_code& error, std::size_t len);
//	void on_read_ssl_data(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len);


//	void ssl_data_relay(std::shared_ptr<relay_data> w_data);
	void ssl_data_send();

	uint32_t add_new_relay(const std::shared_ptr<raw_relay> &relay);
	// remote
//	void on_ssl_handshake(const boost::system::error_code& error);

//	void on_ssl_connect(const boost::system::error_code& error);
//	void local_ssl_handshake(const boost::system::error_code& error);
//	void start_ssl_connect();

};

#endif
