#include "file_system.h"

void create_file_system(BufferAllocator& ba, size_t total_pages) {
	// Write super block
	SuperBlock sb = SuperBlock {
		.next_key = 1,
		.free_list = {
			.total_pages = total_pages,
			.next_free = 2*PAGE_SIZE,
			.highest_unallocated = 2*PAGE_SIZE,
		},
		.tree_root = 1*PAGE_SIZE,
	};
	{
		auto bp1 = ba.load(0);
		bp1.write(&sb, sizeof(sb), 0);
	}

	// write initial tree root
	BTNode bt = BTNode {
		.header = BTNodeHeader {
			.is_leaf = true,
		}
	};
	{
		printf("sizeof %ld\n", sizeof(bt));
		auto bp1 = ba.load(1*PAGE_SIZE);
		bp1.write(&bt, sizeof(bt), 0);
	}
}

