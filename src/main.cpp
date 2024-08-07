#include "buffer_allocator.h"

#include "file_system.h"

#include <cstring>
#include <cstdlib>

int main(int argc, char** argv) {
	if (argc < 2) return 0;

	FILE* f = fopen("test.dat", "r+");
	if (!f) return -1;

	BufferAllocator ba (f, 20);

	if(strcmp(argv[1], "init") == 0){
		create_file_system(ba, 1000);
	} else if(strcmp(argv[1], "append") == 0) {
		append(ba, argv[2], strlen(argv[2]));
	} else if(strcmp(argv[1], "peek") == 0) {
		peek(ba);
	} else if(strcmp(argv[1], "pop") == 0) {
		pop(ba);
		// BTREE STUFF
	} else if(strcmp(argv[1], "insert") == 0) {
		int key = std::atoi(argv[2]);
		int value = std::atoi(argv[3]);
		insert(ba, key, value);
	} else if(strcmp(argv[1], "search") == 0) {
		int key = std::atoi(argv[2]);
		lookup(ba, key);
	}
}
