#include <cstring>

#include "file_system.h"
#include "page_allocator.h"

void create_file_system(BufferAllocator& ba, size_t total_pages) {
	// Write super block
	SuperBlock sb = SuperBlock {
		.next_key = 1,
		.free_list = {
			.total_pages = total_pages,
			.next_free = 0,
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

// Dumb linked list for testing
struct [[gnu::packed]] FSNode {
	BlockID next { 0 };
	size_t size { 0 };
	char data[PAGE_SIZE - sizeof(BlockID) - sizeof(size)];
};

void append(BufferAllocator& ba, char* data, size_t len) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto new_page = allocate_page(ba);
	auto node = (FSNode*)new_page.data();
	node->next = super_block->tree_root;

	// TODO len check
	node->size = len;
	memcpy(node->data, data, len);

	super_block->tree_root = new_page.id();

	super_block_raw.set_dirty();
	new_page.set_dirty();
}

void peek(BufferAllocator& ba) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto head = ba.load(super_block->tree_root);
	auto node = (FSNode*)head.data();
	printf("peeking: %.*s\n", node->size, node->data);
}

void pop(BufferAllocator& ba) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto head = ba.load(super_block->tree_root);
	auto node = (FSNode*)head.data();
	printf("pop: %.*s\n", node->size, node->data);

	super_block->tree_root = node->next;
	free_page(ba, head.id());

	super_block_raw.set_dirty();
	head.set_dirty();
}
