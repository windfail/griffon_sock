#include <boost/asio.hpp>
#include <memory>
//#include <boost/bind.hpp>
using boost::asio::ip::tcp;

class tcp_connection
{
public:
	tcp_connection(boost::asio::io_context& io) : _sock(io) {}
	tcp::socket& get_sock()
	{
		return _sock;
	}
	static void start(std::shared_ptr<tcp_connection> this_ptr);


private:
	tcp::socket _sock;
	static void handle_write(std::shared_ptr<tcp_connection> this_ptr);//std::shared_ptr<tcp_connection> );
};

class tcp_server
{
public:
	tcp_server(int port) : _io_context(), _acceptor(_io_context, tcp::endpoint(tcp::v4(), port)) {}
	void start_accept();
	void handle_accept(std::shared_ptr<tcp_connection> sock_ptr, const boost::system::error_code& error);
	void run() { _io_context.run(); }

private:
	boost::asio::io_context _io_context;
	tcp::acceptor _acceptor;
};



void tcp_connection::handle_write(std::shared_ptr<tcp_connection> this_ptr)
{
//	boost::asio::async_read(*sock_ptr, boost::asio::buffer())
}
void tcp_connection::start(std::shared_ptr<tcp_connection> this_ptr)
{
	boost::asio::async_write(this_ptr->_sock, boost::asio::buffer("test function"),
				 std::bind(&tcp_connection::handle_write, this_ptr));

}
void tcp_server::handle_accept( std::shared_ptr<tcp_connection> sock_ptr, const boost::system::error_code& error)
{
	if (!error) {
		sock_ptr->start(sock_ptr);
	}

	start_accept();

}
void tcp_server::start_accept()
{
	// tcp_connectionion::pointer new_connection =
	//	tcp_connectionion::create(io_context_);
	auto sock_ptr = std::make_shared<tcp_connection> (_io_context);

	_acceptor.async_accept(sock_ptr->get_sock(),
			       std::bind(&tcp_server::handle_accept, this, (sock_ptr), std::placeholders::_1));
}
int server(int port)
{
	//boost::asio::io_context io_context;
	//tcp::acceptor acceptor(io_context);
	tcp_server server(port);

//	acceptor = tcp::acceptor(io_context, tcp::endpoint(tcp::v4(), port));
	server.start_accept();
	server.run();

	// while(1) {
	//	tcp::socket socket(io_context);
	//	acceptor.accept(socket);

	//	boost::system::error_code ignored_error;
	//	boost::asio::write(socket, boost::asio::buffer("test fuction"), ignored_error);
	// }
	return 0;
}
