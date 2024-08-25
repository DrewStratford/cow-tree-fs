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

/*
 * Directory stuff
 */

void Directory::insert_file(char* name, FSType type, BlockID block) {
	// TODO: this is totally unsafe
	auto dir_ent = (DirEntry*)&data[size];
	dir_ent->data = block;
	dir_ent->name_len = strlen(name);
	dir_ent->type = type;
	memcpy(dir_ent->name, name, dir_ent->name_len);
	size += sizeof(DirEntry) + dir_ent->name_len;
}

std::optional<KeyId> Directory::lookup_file(char* name) {
	auto name_len = strlen(name);
	DirEntry* dir_ent = nullptr;
	for (size_t i = 0; i < this->size; i+= sizeof(DirEntry) + dir_ent->name_len) {
		dir_ent = (DirEntry*)&data[size];
		if (strncmp(name, dir_ent->name, name_len) == 0) {
			auto out = dir_ent->data;
			return out;
		}
	}
	return {};
}

void Directory::list_contents() {
	DirEntry* dir_ent = nullptr;
	for (size_t i = 0; i < this->size; i+= sizeof(DirEntry) + dir_ent->name_len) {
		dir_ent = (DirEntry*)&data[i];
		switch (dir_ent->type) {
			case Unknown:
				printf("U\t");
				break;
			case SmallDir:
				printf("D\t");
				break;
			case SmallFile:
				printf("F\t");
				break;
		}
		printf("%ld, %.*s\n", 
				dir_ent->data, (int)dir_ent->name_len, dir_ent->name);
	}
}

void list_directory(BufferAllocator& ba, KeyId key) {
	auto parent_block = lookup(ba, key);
	if (!parent_block.has_value()) {
		return;
	}

	auto parent_raw = ba.load(parent_block.value());
	auto parent = (Directory*)parent_raw.data();
	parent->list_contents();
}

void create_root_directory(BufferAllocator& ba) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	KeyId new_key = 0; // root is hardcoded to key 0
	super_block->next_key++;

	auto new_dir_raw = allocate_page(ba);
	auto new_dir = (Directory*)new_dir_raw.data();
	*new_dir = Directory{
		.header {
			.key = new_key,
			.block = new_dir_raw,
			.type = SmallDir,
		},
	};
	
	insert(ba, new_key, new_dir_raw.id());

	super_block_raw.set_dirty();
}

std::optional<KeyId> add_directory(BufferAllocator& ba, KeyId parent_key, char* name) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto parent_block = lookup(ba, parent_key);
	if (!parent_block.has_value()) {
		return {};
	}

	auto parent_raw_old = ba.load(parent_block.value());
	auto parent_raw = allocate_page(ba);
	memcpy(parent_raw.data(), parent_raw_old.data(), PAGE_SIZE);
	auto parent = (Directory*)parent_raw.data();
	parent->header.block = parent_raw.id();
	// TODO: check parent type

	auto new_key = super_block->next_key;
	super_block->next_key++;

	auto new_dir_raw = allocate_page(ba);
	auto new_dir = (Directory*)new_dir_raw.data();
	*new_dir = Directory{
		.header {
			.key = new_key,
			.block = new_dir_raw,
			.type = SmallDir,
		},
	};

	parent->insert_file(name, SmallDir, new_key);
	insert(ba, parent_key, parent_raw.id());
	insert(ba, new_key, new_dir_raw.id());
	new_dir_raw.set_dirty();
	parent_raw.set_dirty();

	super_block_raw.set_dirty();
	return new_key;
}
