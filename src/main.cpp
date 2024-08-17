
#define FUSE_USE_VERSION 31
#define _FILE_OFFSET_BITS 64

#include <fuse.h>

#include "buffer_allocator.h"

#include "file_system.h"

#include <cstring>
#include <cstdlib>

#include <vector>
#include <algorithm>
#include <random>
#include <unordered_set>
#include <chrono>

void test_insert(int k, int v) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return;
		BufferAllocator ba (f, 20);
		insert(ba, k, v);
		fclose(f);
}

void test_remove(int k) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return;
		BufferAllocator ba (f, 20);
		remove(ba, k);
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
	auto rng = std::default_random_engine(std::chrono::steady_clock::now().time_since_epoch().count());
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

void test_delete_random(int amount, int to_delete) {
	auto rng = std::default_random_engine(std::chrono::steady_clock::now().time_since_epoch().count());
	//auto rng = std::default_random_engine{};
	std::vector<int> insertees;
	for(int i = 0; i < amount; i++) {
		insertees.push_back(i);
	}
	std::shuffle(std::begin(insertees), std::end(insertees), rng);

	for(auto i : insertees) {
		test_insert(i, i);
	}

	std::shuffle(std::begin(insertees), std::end(insertees), rng);
	std::unordered_set<int> deletes;
	for(int i = 0; i < to_delete; i++) {
		int d = insertees[i];
		deletes.insert(d);
		test_remove(d);
	}

	FILE* f = fopen("test.dat", "r+");
	if (!f) return;
	BufferAllocator ba (f, 20);

	int success = 0;
	for(int i = 0; i < amount; i++) {
		auto res = lookup(ba, i);
		if (i == res && deletes.count(i) == 0)
			success++;
		if (!res.has_value() && deletes.count(i) == 0)
			printf("failed to lookup %d\n", i);
	}
	printf("successful lookups %d\n", success);

	int success_deletes = 0;
	for(auto d : deletes) {
		if (!lookup(ba, d).has_value()) success_deletes++;
	}
	printf("successful deletes %d\n", success_deletes);
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
		auto res = insert(ba, key, value);
		if (res.has_value()) {
			printf("replaced %ld\n", res.value());
		}
	} else if(strcmp(argv[1], "search") == 0) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return -1;
		BufferAllocator ba (f, 20);
		int key = std::atoi(argv[2]);
		auto res = lookup(ba, key);
		if (res.has_value()) {
			printf("found %ld\n", res.value());
		} else {
			printf("not found\n");
		}
	} else if(strcmp(argv[1], "remove") == 0) {
		FILE* f = fopen("test.dat", "r+");
		if (!f) return -1;
		BufferAllocator ba (f, 20);
		int key = std::atoi(argv[2]);
		auto res = remove(ba, key);
		if (res.has_value()) {
			printf("removed %ld\n", res.value());
		} else {
			printf("not found\n");
		}
	} else if(strcmp(argv[1], "test_seq") == 0) {
		int amount = std::atoi(argv[2]);
		test_insert_sequential(amount);
	} else if(strcmp(argv[1], "test_rev") == 0) {
		int amount = std::atoi(argv[2]);
		test_insert_sequential(amount);
	} else if(strcmp(argv[1], "test_rand") == 0) {
		int amount = std::atoi(argv[2]);
		test_insert_random(amount);
	} else if(strcmp(argv[1], "test_delete") == 0) {
		int amount = std::atoi(argv[2]);
		int del = std::atoi(argv[3]);
		test_delete_random(amount, del);
	}
}
