#include "buffer_allocator.h"

#include "file_system.h"

#include <cstring>

int main(int argc, char** argv) {
	if (argc < 2) return 0;

	FILE* f = fopen("test.dat", "r+");
	if (!f) return -1;

	BufferAllocator ba (f, 2);

	if(strcmp(argv[1], "init") == 0){
		create_file_system(ba, 1000);
	}
}
