#pragma once

#include <cstdint>
#include <cstddef>

using BlockID = uint64_t;

const size_t PAGE_SIZE = 4096;

// Free list, could be more optimised
struct [[gnu::packed]] FreeList {
	size_t total_pages { 0 };
	size_t allocated { 0 };
	BlockID next_free { 0 };
	BlockID highest_unallocated { 0 };

	bool is_full() {
		return allocated >= total_pages;
	}
};

struct [[gnu::packed]] FreeListPage {
	BlockID next { 0 };
};


using KeyId = uint64_t;


// General object header
enum Type { Unknown, Test, Node, Dir, File};
struct [[gnu::packed]] Header {
	KeyId key { 0 };
	BlockID block { 0 };
	Type type { Unknown };
};

// BTree node
struct [[gnu::packed]] KeyPair {
	KeyId key { 0 };
	BlockID value { 0 };
};

struct [[gnu::packed]] BTNodeHeader {
	bool is_leaf { false };
	size_t count { 0 };
};

const size_t MAX_KEY_PAIRS = (PAGE_SIZE - sizeof(BTNodeHeader)) / sizeof(KeyPair);
struct [[gnu::packed]] BTNode {
	BTNodeHeader header;
	KeyPair pairs[MAX_KEY_PAIRS];
};

// Test item
const size_t TEST_DATA_SIZE = (PAGE_SIZE - sizeof(Header)) / sizeof(char);
struct [[gnu::packed]] TestNodeHeader {
	Header header;
	char data[];
};

struct [[gnu::packed]] SuperBlock {
	BlockID next_key { 0 };
	FreeList free_list { 0 };
	BlockID tree_root { 0 };
};

