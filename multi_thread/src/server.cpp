#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>

using boost::asio::ip::tcp;


class tcp_connection
	:public std::enable_shared_from_this<tcp_connection>
{
public:
	tcp_connection(boost::asio::io_context& io) : _sock(io), _data_r(), _data_w() {}
	~tcp_connection()  {std::cout << "destruct connection"<< std::endl;}
	tcp::socket& get_sock()
	{
		return _sock;
	}
	void start();


private:
	//tcp::socket _sock;
	tcp::socket _sock;
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
