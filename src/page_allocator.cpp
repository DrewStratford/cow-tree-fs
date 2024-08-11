#include <cstring>

#include "page_allocator.h"


void initiate_page_allocator(BufferAllocator& ba, int total_pages) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	// TODO: do we actually need this function?

}

BufferPointer allocate_page(BufferAllocator& ba) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();
	auto& free_list = super_block->free_list;

	// TODO: proper error handling
	if (free_list.is_full())
		return BufferPointer();

	bool should_bump_water_mark = free_list.next_free == 0;
	// Our free list is empty, we have to increase the watermark.
	if (should_bump_water_mark) {
		free_list.next_free = free_list.highest_unallocated;
		free_list.highest_unallocated += PAGE_SIZE;
	}

	auto next_free = free_list.next_free;

	auto free_block = ba.load(next_free);
	// Point to the next free page (only if we did not bump the watermark!).
	if (should_bump_water_mark) {
		free_list.next_free = 0;
	} else {
		free_list.next_free = ((FreeListPage*)free_block.data())->next;
	}
	// Clear the returned page
	memset(free_block.data(), 0, PAGE_SIZE);

	// The superblock has been edited, so mark it as dirty,
	super_block_raw.set_dirty();

	return free_block;
}

void free_page(BufferAllocator& ba, BlockID block_id) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();
	auto& free_list = super_block->free_list;

	// Freed block is the new head of the list.
	auto to_free_raw = ba.load(block_id);
	auto to_free = (FreeListPage*)to_free_raw.data();
	to_free->next = free_list.next_free;
	free_list.next_free = block_id;

	to_free_raw.set_dirty();
	super_block_raw.set_dirty();
}

void free_pages(BufferAllocator& ba, std::unordered_set<BlockID>& to_free) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();
	auto& free_list = super_block->free_list;

	for (auto block_id : to_free) {
		// Freed block is the new head of the list.
		auto freeing_raw = ba.load(block_id);
		auto freeing = (FreeListPage*)freeing_raw.data();
		freeing->next = free_list.next_free;
		free_list.next_free = block_id;
		freeing_raw.set_dirty();
	}

	super_block_raw.set_dirty();
}
