#include <cstdio>
#include <cstdlib>
#include <regex>

extern "C" void main(int argc, char** argv) {
	FILE* fp = stdin;

	if (argc < 2) {
		printf("Usage: %s <pattern> [file=stdin]\n", argv[0]);
		exit(1);
	}

	if (argc >= 3) {
		fp = fopen(argv[2], "r");
		if (fp == nullptr) {
			fprintf(stderr, "Failed to open: %s\n", argv[2]);
			exit(1);
		}
	}

	std::regex pattern{argv[1]};

	if (!fp) {
		printf("failed to open: %s\n", argv[2]);
		exit(1);
	}

	char line[256] = {};
	while (fgets(line, sizeof line, fp)) {
		std::cmatch m;
		if (std::regex_search(line, m, pattern)) {
			printf("%s", line);
		}
		memset(line, 0, sizeof line);
	}
	exit(0);
}