#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <iostream>
using boost::asio::ip::tcp;

namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

class ssl_connection
	:public std::enable_shared_from_this<ssl_connection>
{
public:
	ssl_connection(boost::asio::io_context& io, ssl::context &ctx) : _sock(io, ctx), _data() {}
	~ssl_connection()  {std::cout << "destruct connection"<< std::endl;}
	ssl_socket& get_sock()
	{
		return _sock;
	}
	void start();


private:
	ssl_socket _sock;
	std::string _data;

	void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred );
};

class tcp_server
{
public:
	tcp_server(int port) : _io_context(), _ctx(ssl::context::tlsv12_server), _acceptor(_io_context, tcp::endpoint(tcp::v4(), port)) {
		_ctx.load_verify_file("yily.crt");
		_ctx.use_certificate_file("yily.crt", ssl::context::pem);

		_ctx.use_rsa_private_key_file("key.pem", ssl::context::pem);
	}
	void start_accept();
	void handle_accept(std::shared_ptr<ssl_connection> sock_ptr, const boost::system::error_code& error);
	void run() { _io_context.run(); }

private:
	boost::asio::io_context _io_context;
	tcp::acceptor _acceptor;
	ssl::context _ctx;

};

void ssl_connection::handle_write(const boost::system::error_code& error, std::size_t bytes_transferred )
{
	//std::cout<<"after read "<< bytes_transferred<<":" <<_data << std::endl;
	if (error != boost::system::errc::success) {
		std::cout << "read error occur " <<std::endl;
		return;
	}
	_data.resize(bytes_transferred);
	//std::istringstream in(_data);
	int id;
	std::string word;
	std::istringstream(_data) >> word >> id;
	std::cout << id << "doing..." <<std::endl;
	sleep(id);

	_data += " modified";
	std::cout<< _data << std::endl;
	auto wr_buf = boost::asio::buffer(_data);
//	std::cout << "write buf:"<<wr_buf.data() << " size " <<wr_buf.size() << std::endl;
	boost::asio::async_write(_sock, wr_buf,
				 std::bind(&ssl_connection::start, shared_from_this()));

}
void ssl_connection::start()
{
	_sock.lowest_layer().set_option(tcp::no_delay(true));
	_sock.set_verify_mode(ssl::verify_peer);
	_sock.handshake(ssl_socket::server);
	//this_ptr->_data.clear();
	_data.resize(128,0);

	auto rd_buf = boost::asio::buffer(_data);
	//std::cout <<"read buf:" << rd_buf.data() << " size " <<rd_buf.size() << std::endl;

	_sock.async_read_some( rd_buf,
			       std::bind(&ssl_connection::handle_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2));

}
void tcp_server::handle_accept( std::shared_ptr<ssl_connection> sock_ptr, const boost::system::error_code& error)
{
	if (!error) {
		sock_ptr->start();
	}

	start_accept();

}
void tcp_server::start_accept()
{
	// ssl_connectionion::pointer new_connection =
	//	ssl_connectionion::create(io_context_);
	auto sock_ptr = std::make_shared<ssl_connection> (_io_context, _ctx);

	_acceptor.async_accept(sock_ptr->get_sock().lowest_layer(),
			       std::bind(&tcp_server::handle_accept, this, (sock_ptr), std::placeholders::_1));
}
int server(int port)
{
	tcp_server server(port);

	server.start_accept();
	server.run();
	return 0;
}
