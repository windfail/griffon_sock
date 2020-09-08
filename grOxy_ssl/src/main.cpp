#include <getopt.h>
#include <cstdlib>
#include <iostream>

int server(int port, int thread_num);

int main(int argc, char*argv[])
{
	int cmd;
	int port = 1080;
	int thread_num = 0;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"port",  required_argument,       0,  1 },
			{"thread",  required_argument,       0,  2 },

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

		}

	}

	server(port, thread_num);

	return 0;

}
