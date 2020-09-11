#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include <vector>
#include <thread>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

//#include "relay_server.hpp"

namespace logging = boost::log;
namespace keywords = boost::log::keywords;

static void init_log()
{

	logging::add_file_log(keywords::file_name = "sample.log",
			      keywords::target_file_name = "sample.log",
			      keywords::auto_flush = true,
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


int local_server(int l_port, int r_port, const std::string & r_ip, int thread_num)
{
	init_log();

	// relay_server l_server(l_port, r_port, r_ip);
	// l_server.init_ssl();
	// l_server.local_start_accept();

	// std::vector<std::thread> server_th;
	// for (int i = 1; i < thread_num-1; i++) {
	// 	server_th.emplace_back([&](){ l_server.run();});
	// }
	// l_server.run();

	return 0;
}

int remote_server(int port, int thread_num)
{
	init_log();

	// relay_server r_server(port);
	// r_server.init_ssl();
	// r_server.remote_start_accept();

	// std::vector<std::thread> server_th;
	// for (int i = 1; i < thread_num-1; i++) {
	// 	server_th.emplace_back([&](){ r_server.run();});
	// }
	// r_server.run();

	return 0;

}
