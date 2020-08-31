#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>

using boost::asio::ip::tcp;

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

	void read_data(tcp::socket *sock_r, tcp::socket *sock_w, data_t *data, const crypt_handle &crypt,
		       const boost::system::error_code &error, std::size_t len);
	void send_data(tcp::socket *sock_r, tcp::socket *sock_w, data_t *data, const crypt_handle &crypt,
		       const boost::system::error_code& error, std::size_t len);

//	void handle_write(tcp::socket *sock_r, tcp::socket *sock_w, data_t &data, const crypt_handle &crypt,
//			  const boost::system::error_code &error, std::size_t len);
	//void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred );
};
// void tcp_relay::handle_write(tcp::socket *sock_r, tcp::socket *sock_w, data_t &data, const crypt_handle &crypt,
// 		const boost::system::error_code &error, std::size_t len)
// {

// 	send_data(sock_r, sock_w, data, boost::asio::buffer(data.out), crypt,
// 		  error, 0);
// }
void tcp_relay::start_relay()
{
	boost::system::error_code init_err;

	read_data(&_raw_sock, &_crypt_sock, &_raw_data, crypt_obj->encrypt,init_err , 0);
	read_data(&_crypt_sock, &_raw_sock, &_crypt_data, crypt_obj->decrypt, init_err, 0);

}

void tcp_relay::read_data(tcp::socket *sock_r, tcp::socket *sock_w, data_t *data, const crypt_handle &crypt,
		const boost::system::error_code &error, std::size_t len)
{
	data->in.resize(128,0);
	async_read(*sock_r, boost::asio::buffer(data->in),
		   boost::asio::bind_executor(_strand,
			     std::bind(&tcp_relay::send_data,
				       shared_from_this(),
				       sock_r, sock_w, data, crypt,
				       std::placeholders::_1, std::placeholders::_2)));
}
void tcp_relay::send_data(tcp::socket *sock_r, tcp::socket *sock_w,
			   data_t *data, const crypt_handle &crypt,
			   const boost::system::error_code& error, std::size_t len)
{
	// if (len == w_buf.size()) {
	// 	read_data(sock_r, sock_w, data, crypt);
	// } else {
//		w_buf += len;
	data->in.resize(len);
	crypt(data->in, data->out);
	async_write(*sock_w, boost::asio::buffer(data->out),
		    boost::asio::bind_executor(_strand,
					 std::bind(&tcp_relay::read_data,
					     shared_from_this(),
					     sock_r, sock_w, data, crypt,
					     std::placeholders::_1, std::placeholders::_2)));
//	}
}



class tcp_connection
	:public std::enable_shared_from_this<tcp_connection>
{
public:
	tcp_connection(boost::asio::io_context& io) : _sock(io), _data_r(4096, 0), _data_w() {}
	~tcp_connection()  {std::cout << "destruct connection"<< std::endl;}
	tcp::socket& get_sock()
	{
		return _sock;
	}
	void start();
	void start_sock();


private:
	//tcp::socket _sock;
	tcp::socket _sock;
	std::string _data_r;
	std::string _data_w;
	void handle_sock(const boost::system::error_code& error, std::size_t rc );
	
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
			       std::bind(&tcp_connection::handle_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2));

}

void tcp_server::handle_accept( std::shared_ptr<tcp_connection> sock_ptr, const boost::system::error_code& error)
{
	if (!error) {
		sock_ptr->start_sock();
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

void tcp_connection::handle_sock(const boost::system::error_code& error, std::size_t rc )
{
	if (error != boost::system::errc::success) {
		std::cout << "read error occur " <<std::endl;
		return;
	}

}
void tcp_connection::start_sock()
{
//	_data_r.clear();
	_data_r.resize(4096,0);
	_sock.receive(boost::asio::buffer(_data_r));
	std::string sock_5{5,0};
	_sock.send(boost::asio::buffer(sock_5));

	_sock.receive(boost::asio::buffer(_data_r, 4));
	if (_data_r[1] != 1) {	// not connect cmd, error
		return;
	}
	switch (_data_r[3]) {
	case 1:
		_sock.receive(boost::asio::buffer(_data_r,4));

		break;

	case 3:{
		_sock.receive(boost::asio::buffer(_data_r,1));
		int len = _sock.receive(boost::asio::buffer(_data_r,_data_r[0]));
		std::cout<<_data_r.substr(0, len)<< std::endl;

		break;
	}
	case 4:
		_sock.receive(boost::asio::buffer(_data_r, 16));
		break;

	}
	pause();

	_sock.receive(boost::asio::buffer(_data_r, 2));
	int server_port = (_data_r[0]<<8) | _data_r[1];

	_sock.async_receive( boost::asio::buffer(_data_r), std::bind(&tcp_connection::handle_sock, shared_from_this(), std::placeholders::_1, std::placeholders::_2));

}

int ss_server(int port)
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

int server(int port)
{
	tcp_server server(port);

	server.start_accept();
	server.run();
	return 0;
	
}
