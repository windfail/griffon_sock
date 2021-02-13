#include <getopt.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <tuple>

void str_strip(std::string & src, const std::string &ch)
{
	auto start = src.find_first_not_of(ch);
	auto end = src.find_last_not_of(ch);
	if (start == std::string::npos) {
		src = "";
		return;
	}
	src = src.substr(start, end-start+1);
	return;
}
std::pair<std::string, std::string> str_split(const std::string & src, const char ch)
{
	auto pos = src.find_first_of(ch);
	if (pos == std::string::npos) {
		return {src, ""	};
	}
	return {src.substr(0, pos), src.substr(pos+1)};

}

int local_server(int l_port, int r_port, const std::string & r_ip, int thread_num);
int remote_server(int port, int thread_num);

const bool LOCAL = true;
const bool REMOTE = false;

int main(int argc, char*argv[])
{
	int cmd;
	int port = 1080;
	int thread_num = 10;
	bool type = LOCAL;
	std::string r_ip = "144.202.86.172";
	int r_port = 10230;
	std::string conf_file = "/etc/groxy_ssl/groxy_ssl.conf";

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"lport",  required_argument,       0,  1 },
			{"thread",  required_argument,       0,  2 },
			{"rport",  required_argument,       0,  3 },
			{"rip",  required_argument,       0,  4 },
			{"remote",  no_argument,       0,  5 },
			{0,         0,                 0,  0 }
		};

		cmd = getopt_long(argc, argv, "p:t:c:",
				long_options, &option_index);
		if (cmd == -1)
			break;

		switch (cmd) {
		case 'c': {
			conf_file = optarg;
			break;
		}

		case 'p':
		case 1: {
			port = atoi(optarg);
			break;
		}
		case 't':
		case 2:
			thread_num = atoi(optarg);
			if (thread_num < 2) {
				std::cout <<"thread num at least 2\n";
				return -1;
			}
			break;
		case 3:
			r_port = atoi(optarg);
			break;
		case 4:
			r_ip = optarg;
			break;
		case 5:
			type = REMOTE;
			break;

		}

	}
	std::string conf;
	std::ifstream conf_in(conf_file);

	for (std::string line; std::getline(conf_in, line); ) {
		std::string key, value;
		std::tie(key, value) = str_split(line, '#');
		std::tie(key, value) = str_split(key, '=');
		str_strip(key, " \t");
		str_strip(value, " \t");
		if (key == "thread_num") {
			thread_num = stoi(value);
		} else if (key == "port") {
			port = stoi(value);
		} else if (key == "local") {
			type = value == "true";
		} else if (key == "server") {
			r_ip = value;
		} else if (key == "server_port") {
			r_port = stoi(value);
		}
	}

	if (type == LOCAL) {
		local_server(port, r_port, r_ip, thread_num);

	} else {
		remote_server(port, thread_num);
	}

	return 0;

}
