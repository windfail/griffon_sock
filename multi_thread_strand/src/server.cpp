#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>

using boost::asio::ip::tcp;

using boost::asio::generic::raw_protocol;

class tcp_connection
	:public std::enable_shared_from_this<tcp_connection>
{
public:
	tcp_connection(boost::asio::io_context& io) : _sock(io), _data_r(), _data_w(), _strand(io) {}
	~tcp_connection()  {std::cout << "destruct connection"<< std::endl;}
	tcp::socket& get_sock()
	{
		return _sock;
	}
	void start();


private:
	//tcp::socket _sock;
	tcp::socket _sock;
	boost::asio::io_context::strand _strand;
	std::string _data_r;
	std::string _data_w;

	void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred );
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

void tcp_connection::handle_write(const boost::system::error_code& error, std::size_t bytes_transferred )
{
	std::cout<<"after read "<< bytes_transferred<<":" <<_data_r << std::endl;
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::cout << "start at" << std::ctime(&start_time) << std::endl;
	if (error != boost::system::errc::success) {
		std::cout << "read error occur " <<std::endl;
		return;
	}
	_data_r.resize(bytes_transferred);
	std::string data = _data_r;
	start();
	//std::istringstream in(_data);
	int id;
	std::string word;
	std::istringstream(data) >> word >> id;
	std::cout << id << "doing..." <<std::endl;
	sleep(id);
	std::ostringstream mod;
	mod << " mod by " <<std::this_thread::get_id();
	data += mod.str();
	std::cout<< data << std::endl;
	std::time_t end = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::cout << "end at" << std::ctime(&end) << std::endl;
	_data_w = data;
	auto wr_buf = boost::asio::buffer(_data_w);
//	std::cout << "write buf:"<<wr_buf.data() << " size " <<wr_buf.size() << std::endl;
	_sock.send(wr_buf);//,
			//	 std::bind(&tcp_connection::start, shared_from_this()));

}
void tcp_connection::start()
{
	//this_ptr->_data.clear();
	_data_r.clear();
	_data_r.resize(128,0);

	auto rd_buf = boost::asio::buffer(_data_r);
	//std::cout <<"read buf:" << rd_buf.data() << " size " <<rd_buf.size() << std::endl;

	_sock.async_receive( rd_buf,
			boost::asio::bind_executor(_strand,
			       std::bind(&tcp_connection::handle_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2)));

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
	std::vector<std::thread> server_th;

	server.start_accept();
	for (int i = 0; i < 10; i++) {
		server_th.emplace_back([&](){ server.run();});
		std::cout << "start thread: "<< i<< std::endl;

	}
	pause();

//	server.run();
	return 0;
}


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

class base_relay
	:public std::enable_shared_from_this<base_relay>
{
public:
	~base_relay()  {std::cout << "destruct relay object"<< std::endl;}
	void start_relay();

private:
	typedef struct { std::string in; std::string out; } data_t;

	raw_protocol::socket _raw_sock;
	raw_protocol::socket _crypt_sock;
	data_t _raw_data;
	data_t _crypt_data;
	std::shared_ptr<gri_crypt> crypt_obj;

	void read_data(raw_protocol::socket *sock_r, raw_protocol::socket *sock_w, data_t &data, const crypt_handle &crypt);
	void send_data(raw_protocol::socket *sock_r, raw_protocol::socket *sock_w,
		       data_t &data, boost::asio::mutable_buffer w_buf, const crypt_handle &crypt,
		       const boost::system::error_code& error, std::size_t len);

	void handle_write(raw_protocol::socket *sock_r, raw_protocol::socket *sock_w, data_t &data, const crypt_handle &crypt,
			  const boost::system::error_code &error, std::size_t len);
	//void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred );
};
void base_relay::handle_write(raw_protocol::socket *sock_r, raw_protocol::socket *sock_w, data_t &data, const crypt_handle &crypt,
		const boost::system::error_code &error, std::size_t len)
{
	data.in.resize(len);
	crypt(data.in, data.out);

	send_data(sock_r, sock_w, data, boost::asio::buffer(data.out), crypt,
		  error, 0);
}
void base_relay::start_relay()
{
	read_data(&_raw_sock, &_crypt_sock, _raw_data, crypt_obj->encrypt);
	read_data(&_crypt_sock, &_raw_sock, _crypt_data, crypt_obj->decrypt);

}

void base_relay::read_data(raw_protocol::socket *sock_r, raw_protocol::socket *sock_w, data_t &data, const crypt_handle &crypt)
{
	data.in.resize(128,0);
	sock_r->async_receive(boost::asio::buffer(data.in),
			     std::bind(&base_relay::handle_write,
				       shared_from_this(),
				       sock_r, sock_w, data, crypt,
				       std::placeholders::_1, std::placeholders::_2));
}
void base_relay::send_data(raw_protocol::socket *sock_r, raw_protocol::socket *sock_w,
			   data_t &data, boost::asio::mutable_buffer w_buf, const crypt_handle &crypt,
			   const boost::system::error_code& error, std::size_t len)
{
	if (len == w_buf.size()) {
		read_data(sock_r, sock_w, data, crypt);
	} else {
		w_buf += len;
		sock_w->async_send(w_buf,
				   std::bind(&base_relay::send_data,
					     shared_from_this(),
					     sock_r, sock_w, data, w_buf, crypt,
					     std::placeholders::_1, std::placeholders::_2));
	}
}
