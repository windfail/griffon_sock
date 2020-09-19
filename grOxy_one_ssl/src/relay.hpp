#ifndef _GROXY_RELAY_HPP
#define _GROXY_RELAY_HPP
#include <random>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <unordered_map>
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

class ssl_relay;

class relay_data
{

public:
	enum command {
		STOP_RELAY,
		START_RELAY,
		DATA_RELAY
	};
	enum stop_src {
		from_ssl,
		from_raw,
		ssl_err
	};
	struct _header_t {
		uint32_t session;
		command cmd;
		size_t len;
		_header_t(uint32_t session, command cmd, size_t len) :session(session), cmd(cmd), len(len) {
		}
	};

private:
	_header_t _header;
	std::string _data;

public:

	//	typedef struct {uint32_t index;command cmd; } _header_t;
	relay_data(uint32_t session) :_data(READ_BUFFER_SIZE,0), _header(session, relay_data::DATA_RELAY, READ_BUFFER_SIZE) {
	}
	relay_data(uint32_t session, command cmd) : _data(), _header(session, cmd, 0) {
	}
	std::string& data() {
		return _data;
	}
	_header_t & head() {
		return _header;
	}
	auto header_buffer() {
		return asio::buffer(&_header, sizeof(_header_t));
	}

	auto data_buffer() {
		return asio::buffer(_data);
	}
	auto buffers() {
		return std::array<asio::mutable_buffer, 2> { asio::buffer(&_header, sizeof(_header_t)), asio::buffer(_data)} ;
	}
	void resize() {
		_data.resize(_header.len);
	}
	void resize(size_t data_len) {
		_header.len = data_len;
		_data.resize(data_len);
	}
	auto header_size() {
		return sizeof(_header_t);
	}
	auto data_size() {
		return _data.size();
	}
	auto size() {
		return _data.size() + sizeof(_header_t);
	}
private:


};

// raw relay , for client to local server and remote server to dest
class raw_relay
	:public std::enable_shared_from_this<raw_relay>
{
public:


	raw_relay(asio::io_context &io, const std::shared_ptr<ssl_relay> &manager) :
		_session (0), _strand(io), _sock(io), _host_resolve(io), _manager(manager)
		{
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
	void on_raw_send(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len);

	void on_raw_read(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len);

	void on_local_addr_ok(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);
	void on_local_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);
	void start_local_addr_get(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);
	void local_on_start(std::shared_ptr<std::string> buf, const boost::system::error_code& error, std::size_t len);

	void on_remote_connect(const boost::system::error_code& error);

};

// ssl relay , maintain tls between local server and remote server
class ssl_relay
	:public std::enable_shared_from_this<ssl_relay>
{
public:

	// remote ssl relay
	ssl_relay(asio::io_context &io, ssl::context &ctx) :
		_io_context(io), _strand(io), _sock(io, ctx),_acceptor(io),
		_remote(), _started(false), _rand(std::random_device()())
	{
		BOOST_LOG_TRIVIAL(debug) << "ssl relay construct";
//		std::random_device rd;

	}

	// local ssl relay
	ssl_relay(asio::io_context &io, ssl::context &ctx, const tcp::endpoint &remote, int local_port) :
		_io_context(io), _strand(io), _sock(io, ctx),_acceptor(io, tcp::endpoint(tcp::v4(), local_port)),
		_remote(remote), _started(false),_rand(std::random_device()())
	{
		BOOST_LOG_TRIVIAL(debug) << "ssl relay construct";
	}
	~ssl_relay()  {
		BOOST_LOG_TRIVIAL(debug) << "ssl relay destruct";
	};

//	void local_start();
//	void remote_start();

	asio::io_context::strand & get_strand() {
		return _strand;
	}
	void stop_ssl_relay(uint32_t session, relay_data::stop_src src);
	void send_data_on_ssl(std::shared_ptr<relay_data> buf);
	void start_ssl_data_relay();
	ssl_socket & get_sock() {return _sock;}

	void local_start_accept();
	void start_new_relay(std::shared_ptr<relay_data> buf);

	void remote_ssl_start();

private:
	class _relay_t {
	public:
		std::shared_ptr<raw_relay> relay;
		int timeout;
		_relay_t() :relay(nullptr), timeout(60) {
		};
		_relay_t(const std::shared_ptr<raw_relay> &relay) :relay(relay), timeout(60) {};
		~_relay_t();
	};
	asio::io_context & _io_context;
	bool _started;

	asio::io_context::strand _strand;
	tcp::acceptor _acceptor;
	ssl_socket  _sock;

	std::unordered_map<uint32_t, _relay_t> _relays;

	tcp::endpoint _remote;	// remote ssl relay ep

	// random
	std::minstd_rand _rand;

	void on_read_ssl_header(std::shared_ptr<relay_data> w_data, const boost::system::error_code& error, std::size_t len);
	void on_read_ssl_data(std::shared_ptr<relay_data> buf, const boost::system::error_code& error, std::size_t len);

	void on_write_ssl(std::shared_ptr<relay_data> data, const boost::system::error_code& error, std::size_t len);
	void ssl_data_relay(std::shared_ptr<relay_data> w_data);

	void local_handle_accept(std::shared_ptr<raw_relay> relay, const boost::system::error_code& error);
	uint32_t add_new_relay(const std::shared_ptr<raw_relay> &relay);

	// remote
	void on_ssl_handshake(const boost::system::error_code& error);

};

#endif
