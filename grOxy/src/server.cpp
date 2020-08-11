#include <boost/asio.hpp>
#include <memory>
#include <iostream>

using boost::asio::ip::tcp;

class base_relay
	:public std::enable_shared_from_this<tcp_connection>
{
public:
	~base_relay()  {std::cout << "destruct relay object"<< std::endl;}

	void start_relay();


private:
	typedef struct { std::string in; std::string out; } data_t;
	tcp::socket _raw_sock;
	tcp::socket _crypt_sock;
	data_t _raw_data;
	data_t _crypt_data;
	void read_data(const tcp::socket&, handle);
	void send_data(const tcp::socket&, handle);
	void handle_write(const tcp::socket&, crypt_handle, data_in, data_out);
	void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred );
};
void base_relay::handle_write(const tcp::socket &sock_r, const tcp::socket &sock_w, data_t &data, crypt_handle, 
		const boost::system::error_code &error, std::size_t len)
{
	crypt_handle(data);
	send_data(sock_r, sock_w, data, crypt_handle, boost::system::errc::success, 0);
}
void base_relay::start_relay()
{
	read_data(_raw_sock, _crypt_sock, _raw_data, encrypt);
	read_data(_crypt_sock, _raw_sock, _crypt_data, decrypt);
}
void base_relay::read_data(const tcp::socket & sock_r, const tcp::socket &sock_w, data_t &data, crypt_handle)
{
	sock_r.async_recv(data.in, handle_write(sock_r, sock_w, data, crypt_handle));
}
void base_relay::send_data(const tcp::socket &sock_r, const tcp::socket &sock_w, data_t &data, crypt_handle,
		const boost::system::error_code& error, std::size_t len)
{
	if (len == buf) {
		read_data(sock_r, sock_w, data, crypt_handle);
	} else {
		sock_w.async_send(data.out, send_data(sock_r, sock_w, data, crypt_handle));
	}
}

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

void tcp_connection::handle_write(const boost::system::error_code& error, std::size_t bytes_transferred )
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
				 std::bind(&tcp_connection::start, shared_from_this()));

}
void tcp_connection::start()
{
	//this_ptr->_data.clear();
	_data.resize(128,0);

	auto rd_buf = boost::asio::buffer(_data);
	//std::cout <<"read buf:" << rd_buf.data() << " size " <<rd_buf.size() << std::endl;

	_sock.async_read_some( rd_buf,
			       std::bind(&tcp_connection::handle_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2));

}
void tcp_server::handle_accept( std::shared_ptr<tcp_connection> sock_ptr, const boost::system::error_code& error)
{
	if (!error) {
		sock_ptr->start();
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
	tcp_server server(port);

	server.start_accept();
	server.run();
	return 0;
}
