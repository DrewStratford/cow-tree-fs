#include "buffer_allocator.h"

#include "file_system.h"

#include <cstring>
#include <cstdlib>

#include <vector>
#include <algorithm>
#include <random>

void test_insert(int k, int v) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return;
		BufferAllocator ba (f, 20);
		insert(ba, k, v);
		fclose(f);
}

void test_insert_sequential(int amount) {
	for(int i = 0; i < amount; i++) {
		test_insert(i, i);
	}
	FILE* f = fopen("test.dat", "r+");
	if (!f) return;
	BufferAllocator ba (f, 20);

	for(int i = 0; i < amount; i++) {
		lookup(ba, i);
	}
	int success = 0;
	for(int i = 0; i < amount; i++) {
		if (i == lookup(ba, i)) success++;
	}
	printf("successful lookups %d\n", success);
}

void test_insert_sequential_rev(int amount) {
	for(int i = amount-1; i >= 0 ; i--) {
		test_insert(i, i);
	}

	FILE* f = fopen("test.dat", "r+");
	if (!f) return;
	BufferAllocator ba (f, 20);

	int success = 0;
	for(int i = 0; i < amount; i++) {
		if (i == lookup(ba, i)) success++;
	}
	printf("successful lookups %d\n", success);
}


void test_insert_random(int amount) {
	std::vector<int> insertees;
	for(int i = 0; i < amount; i++) {
		insertees.push_back(i);
	}
	auto rng = std::default_random_engine {};
	std::shuffle(std::begin(insertees), std::end(insertees), rng);

	for(auto i : insertees) {
		test_insert(i, i);
	}

	FILE* f = fopen("test.dat", "r+");
	if (!f) return;
	BufferAllocator ba (f, 20);

	int success = 0;
	for(int i = 0; i < amount; i++) {
		if (i == lookup(ba, i)) success++;
	}
	printf("successful lookups %d\n", success);
}

int main(int argc, char** argv) {
	if (argc < 2) return 0;



	if(strcmp(argv[1], "init") == 0){
		FILE* f = fopen("test.dat", "r+");
		if (!f) return -1;
		BufferAllocator ba (f, 20);
		create_file_system(ba, 1000);
		// BTREE STUFF
	} else if(strcmp(argv[1], "insert") == 0) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return -1;
		BufferAllocator ba (f, 20);
		int key = std::atoi(argv[2]);
		int value = std::atoi(argv[3]);
		insert(ba, key, value);
	} else if(strcmp(argv[1], "search") == 0) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return -1;
		BufferAllocator ba (f, 20);
		int key = std::atoi(argv[2]);
		auto res = lookup(ba, key);
		printf("result is %ld\n", res);
	} else if(strcmp(argv[1], "remove") == 0) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return -1;
		BufferAllocator ba (f, 20);
		int key = std::atoi(argv[2]);
		auto res = remove(ba, key);
		printf("result is %ld\n", res);
	} else if(strcmp(argv[1], "test_seq") == 0) {
		int amount = std::atoi(argv[2]);
		test_insert_sequential(amount);
	} else if(strcmp(argv[1], "test_rev") == 0) {
		int amount = std::atoi(argv[2]);
		test_insert_sequential(amount);
	} else if(strcmp(argv[1], "test_rand") == 0) {
		int amount = std::atoi(argv[2]);
		test_insert_sequential(amount);
	}
}
