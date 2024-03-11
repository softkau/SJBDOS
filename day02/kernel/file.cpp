#include "file.hpp"
#include "font.hpp"

size_t ReadDelim(FileDescriptor& fd, char delim, char* dst, size_t len) {
	size_t idx = 0;
	for (; idx < len - 1; ++idx) {
		if (fd.Read(dst + idx, 1) == 0) break;
		if (dst[idx] == delim) {
			idx++;
			break;
		}
	}
	dst[idx] = '\0';
	return idx;
}