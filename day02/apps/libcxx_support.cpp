#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstdint>

extern "C" int posix_memalign(void** memptr, size_t alignment, size_t size) {
	if (alignment && (!(alignment&(alignment-1)))) return EINVAL;

	void* p = malloc(size + alignment - 1);
	if (!p) {
		return ENOMEM;
	}
	uintptr_t addr = (uintptr_t)p;
	*memptr = (void*)((addr + alignment - 1) & ~(uintptr_t)(alignment - 1));
	return 0;
}
