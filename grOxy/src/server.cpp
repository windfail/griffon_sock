#include <boost/asio.hpp>
#include <memory>
#include <iostream>

using boost::asio::ip::tcp;
using boost::asio::generic::raw_protocol;

typedef std::function<void(std::string&, std::string&)> crypt_handle;
void nocrypt(std::string &src, std::string &dest)
{
	dest = std::move(src);
};

class gri_crypt
{
public:
	crypt_handle encrypt;
	crypt_handle decrypt;
	gri_crypt(const crypt_handle &en=nocrypt, const crypt_handle &de = nocrypt): encrypt(en), decrypt(de) {
	}
};

class tcp_relay
	:public std::enable_shared_from_this<tcp_relay>
{
public:
	tcp_relay(boost::asio::io_context &io, 
			std::shared_ptr<gri_crypt> crypt) :
	       	_strand(io), _raw_sock(io), _crypt_sock(io), crypt_obj(crypt) {}
	~tcp_relay()  {std::cout << "destruct relay object"<< std::endl;}
	void start_relay();

private:
	typedef struct { std::string in; std::string out; } data_t;

	boost::asio::io_context::strand _strand;
	tcp::socket _raw_sock;
	tcp::socket _crypt_sock;
	data_t _raw_data;
	data_t _crypt_data;
	std::shared_ptr<gri_crypt> crypt_obj;

	void read_data(tcp::socket *sock_r, tcp::socket *sock_w, data_t &data, const crypt_handle &crypt);
	void send_data(tcp::socket *sock_r, tcp::socket *sock_w,
		       data_t &data, boost::asio::mutable_buffer w_buf, const crypt_handle &crypt,
		       const boost::system::error_code& error, std::size_t len);

	void handle_write(tcp::socket *sock_r, tcp::socket *sock_w, data_t &data, const crypt_handle &crypt,
			  const boost::system::error_code &error, std::size_t len);
	//void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred );
};
void tcp_relay::handle_write(tcp::socket *sock_r, tcp::socket *sock_w, data_t &data, const crypt_handle &crypt,
		const boost::system::error_code &error, std::size_t len)
{
	data.in.resize(len);
	crypt(data.in, data.out);

	send_data(sock_r, sock_w, data, boost::asio::buffer(data.out), crypt,
		  error, 0);
}
void tcp_relay::start_relay()
{
	read_data(&_raw_sock, &_crypt_sock, _raw_data, crypt_obj->encrypt);
	read_data(&_crypt_sock, &_raw_sock, _crypt_data, crypt_obj->decrypt);

}

void tcp_relay::read_data(tcp::socket *sock_r, tcp::socket *sock_w, data_t &data, const crypt_handle &crypt)
{
	data.in.resize(128,0);
	sock_r->async_receive(boost::asio::buffer(data.in),
			boost::bind_executor(_strand,
			     std::bind(&tcp_relay::handle_write,
				       shared_from_this(),
				       sock_r, sock_w, data, crypt,
				       std::placeholders::_1, std::placeholders::_2)));
}
void tcp_relay::send_data(tcp::socket *sock_r, tcp::socket *sock_w,
			   data_t &data, boost::asio::mutable_buffer w_buf, const crypt_handle &crypt,
			   const boost::system::error_code& error, std::size_t len)
{
	if (len == w_buf.size()) {
		read_data(sock_r, sock_w, data, crypt);
	} else {
		w_buf += len;
		sock_w->async_send(w_buf,
				boost::bind_executor(_strand,
				   std::bind(&tcp_relay::send_data,
					     shared_from_this(),
					     sock_r, sock_w, data, w_buf, crypt,
					     std::placeholders::_1, std::placeholders::_2)));
	}
}

// class tcp_server
// {
// public:
// 	tcp_server(int port) : _io_context(), _acceptor(_io_context, tcp::endpoint(tcp::v4(), port)) {}
// 	void start_accept();
// 	void handle_accept(std::shared_ptr<tcp_connection> sock_ptr, const boost::system::error_code& error);
// 	void run() { _io_context.run(); }

// private:
// 	boost::asio::io_context _io_context;
// 	tcp::acceptor _acceptor;
// };

// void tcp_server::handle_accept( std::shared_ptr<tcp_connection> sock_ptr, const boost::system::error_code& error)
// {
// 	if (!error) {
// 		sock_ptr->start();
// 	}

// 	start_accept();

// }
// void tcp_server::start_accept()
// {
// 	// tcp_connectionion::pointer new_connection =
// 	//	tcp_connectionion::create(io_context_);
// 	auto sock_ptr = std::make_shared<tcp_connection> (_io_context);

// 	_acceptor.async_accept(sock_ptr->get_sock(),
// 			       std::bind(&tcp_server::handle_accept, this, (sock_ptr), std::placeholders::_1));
// }
// int server(int port)
// {
// 	tcp_server server(port);

// 	server.start_accept();
// 	server.run();
// 	return 0;
// }
