#include <vector>

#include "BTree.h"
#include "page_allocator.h"

BufferPointer new_empty_leaf(BufferAllocator& ba) {
	auto new_page = allocate_page(ba);
	auto node = (BTNode*)new_page.data();
	*node = BTNode {
		.header = BTNodeHeader {
			.is_leaf= true,
			.count = 0,
		}
	};

	for (int i = 0; i < MAX_KEY_PAIRS; i++) {
		node->pairs[i].key = MAX_KEY_ID;
	}

	new_page.set_dirty();
	return new_page;
}

BufferPointer new_empty_node(BufferAllocator& ba) {
	auto new_page = allocate_page(ba);
	auto node = (BTNode*)new_page.data();
	*node = BTNode {
		.header = BTNodeHeader {
			.is_leaf= false,
			.count = 0,
		}
	};

	for (int i = 0; i < MAX_KEY_PAIRS; i++) {
		node->pairs[i].key = MAX_KEY_ID;
	}

	new_page.set_dirty();
	return new_page;
}

BufferPointer clone_node(BufferAllocator& ba, BTNode* old_node) {
	auto new_page = allocate_page(ba);
	auto node = (BTNode*)new_page.data();
	*node = *old_node;

	new_page.set_dirty();
	return new_page;
}

BlockID search_btree(BufferAllocator& ba, BlockID id, KeyId key) {
	auto node_raw = ba.load(id);
	auto node = (BTNode*)node_raw.data();

	if (node->header.is_leaf) {
		return search_leaf(node, key);
	} else {
		return search_node(ba, node, key);
	}
}

BlockID search_leaf(BTNode* node, KeyId key) {
	for (int i = 0; i < node->header.count; i++) {
		if (node->pairs[i].key == key) {
			return node->pairs[i].value;
		}
	}
	return -1;
}

BlockID search_node(BufferAllocator& ba, BTNode* node, KeyId key) {
	if (node->header.count < 1 ) {
		return -1;
	}

	int i = 0;
	for (; i < node->header.count; i++) {
		if (key < node->pairs[i].key) {
			BlockID subleaf_id = node->pairs[i].value;
			return search_btree(ba, subleaf_id, key);
		}
	}
	return -1;
}

InsertPropagation insert_btree(BufferAllocator& ba, BlockID id, KeyPair key_pair) {
	auto node_raw = ba.load(id);
	auto node = (BTNode*)node_raw.data();

	if (node->header.is_leaf) {
		return insert_leaf(ba,node, key_pair);
	} else {
		return insert_node(ba, node, key_pair);
	}
}

InsertPropagation insert_leaf(BufferAllocator& ba, BTNode* node, KeyPair key_pair) {
	std::vector<KeyPair> temp_key_pairs;
	bool pushed = false;
	for (int i = 0; i < node->header.count; i++) {
		if (!pushed && key_pair.key < node->pairs[i].key) {
			pushed = true;
			temp_key_pairs.push_back(key_pair);
		}
		temp_key_pairs.push_back(node->pairs[i]);
	}
	if (!pushed) {
		temp_key_pairs.push_back(key_pair);
	}
	
	if (temp_key_pairs.size() <= MAX_KEY_PAIRS) {
		// We don't split

		// TODO: this should probably be a function
		auto new_leaf_raw = new_empty_leaf(ba);
		auto new_leaf = (BTNode*)new_leaf_raw.data();
		new_leaf->header.count = temp_key_pairs.size();
		new_leaf->header.is_leaf = true;

		for (int i = 0; i < temp_key_pairs.size(); i++) {
			new_leaf->pairs[i] = temp_key_pairs[i];
		}

		new_leaf_raw.set_dirty();

		return InsertPropagation {
			.is_split = false,
			.update = new_leaf_raw.id(),
		};
	} else {
		//perform a split

		auto split_at = temp_key_pairs.size()/2;
		auto promoting = temp_key_pairs[split_at].key;

		auto new_left_raw = new_empty_leaf(ba);
		auto new_left = (BTNode*)new_left_raw.data();
		new_left->header.is_leaf = true;

		// TODO: consider the split here
		for (int i = 0; i < split_at; i++) {
			new_left->pairs[i] = temp_key_pairs[i];
			new_left->header.count++;
		}

		auto new_right_raw = new_empty_leaf(ba);
		auto new_right = (BTNode*)new_right_raw.data();
		new_right->header.is_leaf = true;

		for (int i = split_at; i < temp_key_pairs.size(); i++) {
			new_right->pairs[i-split_at] = temp_key_pairs[i];
			new_right->header.count++;
		}

		new_left_raw.set_dirty();
		new_right_raw.set_dirty();

		return InsertPropagation {
			.is_split = true,
			.key = promoting,
			.left = new_left_raw.id(),
			.right = new_right_raw.id(),
		};
	}
}

InsertPropagation insert_node(BufferAllocator& ba, BTNode* node, KeyPair key_pair) {

	int i = 0;
	for (; i < node->header.count; i++) {
		if (key_pair.key <= node->pairs[i].key) {
			break;
		}
	}

	BlockID subtree = node->pairs[i].value;
	auto insert_prop = insert_btree(ba, subtree, key_pair);

	if (insert_prop.is_split) {
		std::vector<KeyPair> temp_key_pairs;
		for (int j = 0; j < node->header.count; j++) {
			// also push in the insertee
			if (j == i) {
				temp_key_pairs.push_back(KeyPair {
						.key = insert_prop.key,
						.value = insert_prop.left,
						});
				temp_key_pairs.push_back(node->pairs[j]);
				temp_key_pairs[temp_key_pairs.size()-1].value = insert_prop.right;
			} else {
				temp_key_pairs.push_back(node->pairs[j]);
			}

		}

		// test for split and propagate.
		if (temp_key_pairs.size() >= MAX_KEY_PAIRS) {
			// TODO split and propagate
			auto split_at = temp_key_pairs.size() / 2;
			auto promoting = temp_key_pairs[split_at];

			auto new_left_raw = new_empty_node(ba);
			auto new_left = (BTNode*)new_left_raw.data();

			// TODO: consider the split here
			for (int i = 0; i <= split_at; i++) {
				new_left->pairs[i] = temp_key_pairs[i];
				new_left->header.count++;
			}
			new_left->pairs[split_at].key = MAX_KEY_ID;

			auto new_right_raw = new_empty_node(ba);
			auto new_right = (BTNode*)new_right_raw.data();

			for (int i = split_at+1; i < temp_key_pairs.size(); i++) {
				new_right->pairs[i-(split_at+1)] = temp_key_pairs[i];
				new_right->header.count++;
			}

			new_left_raw.set_dirty();
			new_right_raw.set_dirty();

			return InsertPropagation{
				.is_split = true,
				.key = promoting.key,
				.left = new_left_raw.id(),
				.right = new_right_raw.id(),
			};
			
		} else { 
			// we don't split
			// TODO: this should probably be a function
			auto new_node_raw = new_empty_node(ba);
			auto new_node = (BTNode*)new_node_raw.data();
			new_node->header.count = temp_key_pairs.size();

			for (int i = 0; i < temp_key_pairs.size(); i++) {
				new_node->pairs[i] = temp_key_pairs[i];
			}

			new_node_raw.set_dirty();

			return InsertPropagation {
				.is_split = false,
				.update = new_node_raw.id(),
			};
		}

	} else {
		// No split, just update to node to point at new child
		auto new_node_raw = clone_node(ba, node);
		auto new_node = (BTNode*)new_node_raw.data();
		new_node->pairs[i].value = insert_prop.update;

		new_node_raw.set_dirty();

		return InsertPropagation {
			.is_split = false,
			.update = new_node_raw.id(),
		};
	}
}
