#pragma once

#include <unordered_set>
#include <optional>

#include "buffer_allocator.h"
#include "definitions.h"

/*
 * A copy on write b tree implementation.
 * This is essentially a b+ tree without the neighbour relation.
 */

// BTree node
struct [[gnu::packed]] KeyPair {
	KeyId key { 0 };
	BlockID value { 0 };
};

struct [[gnu::packed]] BTNodeHeader {
	bool is_leaf { false };
	size_t count { 0 };
};

//const size_t MAX_KEY_PAIRS = (PAGE_SIZE - sizeof(BTNodeHeader)) / sizeof(KeyPair);
const size_t MAX_KEY_PAIRS = 6;
struct [[gnu::packed]] BTNode {
	BTNodeHeader header;
	KeyPair pairs[MAX_KEY_PAIRS];

	bool enough_entries();
	bool can_share_entry();
};

BufferPointer new_empty_leaf(BufferAllocator& ba);
BufferPointer new_empty_node(BufferAllocator& ba);

BufferPointer clone_node(BufferAllocator& ba, BTNode* node);

std::optional<BlockID> search_btree(BufferAllocator& ba, BlockID id, KeyId key);
std::optional<BlockID> search_leaf(BTNode* node, KeyId key);
std::optional<BlockID> search_node(BufferAllocator& ba, BTNode* node, KeyId key);

struct InsertPropagation {
	bool is_split { false };
	// set on split
	KeyId key { 0 };
	BlockID left { 0 };
	BlockID right { 0 };
	// set on non split
	BlockID update { 0 };

	bool did_replace { false };
	BlockID replaced { 0 };
};

InsertPropagation insert_btree(BufferAllocator& ba, std::unordered_set<BlockID>& free, BlockID id, KeyPair key_pair);
InsertPropagation insert_leaf(BufferAllocator& ba, std::unordered_set<BlockID>& free, BTNode* node, KeyPair key_pair);
InsertPropagation insert_node(BufferAllocator& ba, std::unordered_set<BlockID>& free, BTNode* node, KeyPair key_pair);

struct DeletePropagation {
	bool did_modify { false };
	BlockID deleted_value { 0 };
	BufferPointer new_child;
};

DeletePropagation delete_btree(BufferAllocator& ba, std::unordered_set<BlockID>& free, BlockID id, KeyId key);
DeletePropagation delete_leaf(BufferAllocator& ba, std::unordered_set<BlockID>& free, BTNode* node, KeyId key);
DeletePropagation delete_node(BufferAllocator& ba, std::unordered_set<BlockID>& free, BTNode* node, KeyId key);
