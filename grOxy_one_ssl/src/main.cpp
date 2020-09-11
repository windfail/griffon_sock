#include <getopt.h>
#include <cstdlib>
#include <iostream>

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
	std::string r_ip = "192.168.1.20";
	int r_port = 10230;

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

		cmd = getopt_long(argc, argv, "p:t:",
				long_options, &option_index);
		if (cmd == -1)
			break;

		switch (cmd) {
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
	if (type == LOCAL) {
		local_server(port, r_port, r_ip, thread_num);

	} else {
		remote_server(r_port, thread_num);
	}

	return 0;

}
