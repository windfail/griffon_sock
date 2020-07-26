#include <getopt.h>
#include <cstdlib>
int server(int port);
int client(int port);

int main(int argc, char*argv[])
{
	int cmd;
	int port = 10230;
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"client",     optional_argument, 0,  1 },
			{"server",  optional_argument,       0,  2 },
			{0,         0,                 0,  0 }
		};

		cmd = getopt_long(argc, argv, "cs",
				long_options, &option_index);
		if (cmd == -1)
			break;

		switch (cmd) {
		case 1:
			if (optarg) {
				port = atoi(optarg);
			}

			client(port);
			break;
		case 2:
			if (optarg) {
				port = atoi(optarg);
			}
			server(port);

			break;

		}
	}

	return 0;

}
