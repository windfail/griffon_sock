#include <getopt.h>
#include <cstdlib>
#include <iostream>

int server(int port);
int client(const std::string &, int cid);

int main(int argc, char*argv[])
{
	int cmd;
	int port = 10230;
	std::string server_ip = "192.168.1.20";
	int is_client = 1;
	int cid = 0;
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"client",     optional_argument, 0,  1 },
			{"server",  optional_argument,       0,  2 },
			{"ip", required_argument, 0, 3},
			{0,         0,                 0,  0 }
		};

		cmd = getopt_long(argc, argv, "cs",
				long_options, &option_index);
		if (cmd == -1)
			break;

		switch (cmd) {
		case 3: {
			server_ip = optarg;

			break;
		}

		case 1: {

			//std::cout << "cid" <<cid <<std::endl;
			if (optarg) {
				cid = atoi(optarg);
				//std::cout << optarg <<std::endl;
			}

//			client(port, cid);
			break;
		}

		case 2:
			if (optarg) {
				port = atoi(optarg);
			}
			server(port);

			break;

		}
	}
	if (is_client) {
		client(server_ip, cid);
	}

	return 0;

}
