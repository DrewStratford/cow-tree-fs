#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

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
const KeyId MAX_KEY_ID = std::numeric_limits<uint64_t>::max();



struct [[gnu::packed]] SuperBlock {
	BlockID next_key { 0 };
	FreeList free_list { 0 };
	BlockID tree_root { 0 };
};

