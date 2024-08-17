#include <cstring>

#include <optional>

#include "file_system.h"
#include "page_allocator.h"
#include "BTree.h"

void create_file_system(BufferAllocator& ba, size_t total_pages) {
	auto sb_raw = ba.load(0);
	auto sb = (SuperBlock*)sb_raw.data();
	// Write super block
	*sb = SuperBlock {
		.next_key = 1,
		.free_list = {
			.total_pages = total_pages,
			.next_free = 0,
			.highest_unallocated = 1*PAGE_SIZE,
		},
	};
	sb_raw.set_dirty();

	auto initial_root = new_empty_leaf(ba);
	sb->tree_root = initial_root.id();
}

std::optional<BlockID> lookup(BufferAllocator& ba, KeyId key) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto result = search_btree(ba, super_block->tree_root, key);
	return result;
}

std::optional<BlockID> remove(BufferAllocator& ba, KeyId key) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	std::unordered_set<BlockID> to_free;
	auto propagation = delete_btree(ba, to_free, super_block->tree_root, key);

	if (propagation.did_modify) {
		auto new_root_raw = propagation.new_child;
		auto new_root = (BTNode*) new_root_raw.data();
		if (!new_root->header.is_leaf && new_root->header.count == 1) {
			to_free.insert(new_root_raw.id());
			super_block->tree_root = new_root->pairs[0].value;
		} else {
			super_block->tree_root = propagation.new_child.id();
		}
		free_pages(ba, to_free);
		super_block_raw.set_dirty();
		return propagation.deleted_value;
	}

	return {};
}

std::optional<BlockID> insert(BufferAllocator& ba, KeyId key, BlockID value) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto old_root = super_block->tree_root;
	std::unordered_set<BlockID> to_free;
	auto propagation = insert_btree(ba, to_free, super_block->tree_root, KeyPair {
				.key = key,
				.value = value,
			});

	if (propagation.is_split) {
		// Make a new root
		auto new_root_raw = new_empty_node(ba);
		auto new_root = (BTNode*)new_root_raw.data();
		new_root->header.count = 2;
		new_root->pairs[0] = KeyPair {
			.key = propagation.key,
			.value = propagation.left,
		};
		new_root->pairs[1] = KeyPair {
			.key = MAX_KEY_ID,
			.value = propagation.right,
		};
		new_root_raw.set_dirty();
		super_block->tree_root = new_root_raw.id();

	} else {
		super_block->tree_root = propagation.update;
	}

	// Free all of the copied blocks (in the future we could store these in a snapshot).
	to_free.insert(old_root);
	free_pages(ba, to_free);
	
	super_block_raw.set_dirty();

	if (propagation.did_replace) {
		return propagation.replaced;
	}
	return {};
}
