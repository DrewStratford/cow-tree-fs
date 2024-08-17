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

	for (size_t i = 0; i < MAX_KEY_PAIRS; i++) {
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

	for (size_t i = 0; i < MAX_KEY_PAIRS; i++) {
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

bool BTNode::enough_entries() {
	return this->header.count >= MAX_KEY_PAIRS/2;
}

bool BTNode::can_share_entry(){
	return this->header.count >= (MAX_KEY_PAIRS/2)+1;
}

std::optional<BlockID> search_btree(BufferAllocator& ba, BlockID id, KeyId key) {
	auto node_raw = ba.load(id);
	auto node = (BTNode*)node_raw.data();

	if (node->header.is_leaf) {
		return search_leaf(node, key);
	} else {
		return search_node(ba, node, key);
	}
}

std::optional<BlockID> search_leaf(BTNode* node, KeyId key) {
	for (size_t i = 0; i < node->header.count; i++) {
		if (node->pairs[i].key == key) {
			auto resp = node->pairs[i].value;
			return resp;
		}
	}
	return {};
}

std::optional<BlockID> search_node(BufferAllocator& ba, BTNode* node, KeyId key) {
	if (node->header.count < 1 ) {
		return {};
	}

	size_t i = 0;
	for (; i < node->header.count; i++) {
		if (key < node->pairs[i].key) {
			BlockID subleaf_id = node->pairs[i].value;
			return search_btree(ba, subleaf_id, key);
		}
	}
	return {};
}

InsertPropagation insert_btree(BufferAllocator& ba, std::unordered_set<BlockID>& freed, BlockID id, KeyPair key_pair) {
	auto node_raw = ba.load(id);
	auto node = (BTNode*)node_raw.data();

	// Allow the page allocator to reclaim the copied page once
	// this insert is complete
	freed.insert(id);

	if (node->header.is_leaf) {
		return insert_leaf(ba, freed, node, key_pair);
	} else {
		return insert_node(ba, freed, node, key_pair);
	}
}

InsertPropagation insert_leaf(BufferAllocator& ba, std::unordered_set<BlockID>& freed, BTNode* node, KeyPair key_pair) {
	std::vector<KeyPair> temp_key_pairs;
	bool pushed = false;
	bool did_replace = false;
	BlockID replaced = 0;	
	for (size_t i = 0; i < node->header.count; i++) {
		if (!pushed && key_pair.key == node->pairs[i].key) {
			pushed = true;
			did_replace = node->pairs[i].value != key_pair.value;
			replaced = node->pairs[i].value;
			temp_key_pairs.push_back(key_pair);
		} else if (!pushed && key_pair.key < node->pairs[i].key) {
			pushed = true;
			temp_key_pairs.push_back(key_pair);
			temp_key_pairs.push_back(node->pairs[i]);
		} else {
			temp_key_pairs.push_back(node->pairs[i]);
		}
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

		for (size_t i = 0; i < temp_key_pairs.size(); i++) {
			new_leaf->pairs[i] = temp_key_pairs[i];
		}

		new_leaf_raw.set_dirty();

		return InsertPropagation {
			.is_split = false,
			.update = new_leaf_raw.id(),
			.did_replace = did_replace,
			.replaced = replaced,
		};
	} else {
		//perform a split

		auto split_at = temp_key_pairs.size()/2;
		auto promoting = temp_key_pairs[split_at].key;

		auto new_left_raw = new_empty_leaf(ba);
		auto new_left = (BTNode*)new_left_raw.data();
		new_left->header.is_leaf = true;

		// TODO: consider the split here
		for (size_t i = 0; i < split_at; i++) {
			new_left->pairs[i] = temp_key_pairs[i];
			new_left->header.count++;
		}

		auto new_right_raw = new_empty_leaf(ba);
		auto new_right = (BTNode*)new_right_raw.data();
		new_right->header.is_leaf = true;

		for (size_t i = split_at; i < temp_key_pairs.size(); i++) {
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
			.did_replace = did_replace,
			.replaced = replaced,
		};
	}
}

InsertPropagation insert_node(BufferAllocator& ba, std::unordered_set<BlockID>& freed, BTNode* node, KeyPair key_pair) {

	size_t i = 0;
	for (; i < node->header.count; i++) {
		if (key_pair.key < node->pairs[i].key) {
			break;
		}
	}

	BlockID subtree = node->pairs[i].value;
	auto insert_prop = insert_btree(ba, freed, subtree, key_pair);

	if (insert_prop.is_split) {
		std::vector<KeyPair> temp_key_pairs;
		for (size_t j = 0; j < node->header.count; j++) {
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
			for (size_t i = 0; i <= split_at; i++) {
				new_left->pairs[i] = temp_key_pairs[i];
				new_left->header.count++;
			}
			new_left->pairs[split_at].key = MAX_KEY_ID;

			auto new_right_raw = new_empty_node(ba);
			auto new_right = (BTNode*)new_right_raw.data();

			for (size_t i = split_at+1; i < temp_key_pairs.size(); i++) {
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
				.did_replace = insert_prop.did_replace,
				.replaced = insert_prop.replaced,
			};
			
		} else { 
			// we don't split
			// TODO: this should probably be a function
			auto new_node_raw = new_empty_node(ba);
			auto new_node = (BTNode*)new_node_raw.data();
			new_node->header.count = temp_key_pairs.size();

			for (size_t i = 0; i < temp_key_pairs.size(); i++) {
				new_node->pairs[i] = temp_key_pairs[i];
			}

			new_node_raw.set_dirty();

			return InsertPropagation {
				.is_split = false,
				.update = new_node_raw.id(),
				.did_replace = insert_prop.did_replace,
				.replaced = insert_prop.replaced,
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
			.did_replace = insert_prop.did_replace,
			.replaced = insert_prop.replaced,
		};
	}
}


KeyPair find_min_leaf(BufferAllocator& ba, BTNode* node) {
	if (node->header.count == 0) return KeyPair{};

	return node->pairs[0];
}

KeyPair find_max_leaf(BufferAllocator& ba, BTNode* node) {
	if (node->header.count == 0) return KeyPair{};

	return node->pairs[node->header.count-1];
}

KeyPair find_max_node(BufferAllocator& ba, BTNode* node);
KeyPair find_min_node(BufferAllocator& ba, BTNode* node);

KeyPair find_max_btree(BufferAllocator& ba, BlockID id) {
	auto node_raw = ba.load(id);
	auto node = (BTNode*)node_raw.data();

	if (node->header.is_leaf) {
		return find_max_leaf(ba, node);
	} else {
		return find_max_node(ba, node);
	}
}

KeyPair find_min_btree(BufferAllocator& ba, BlockID id) {
	auto node_raw = ba.load(id);
	auto node = (BTNode*)node_raw.data();

	if (node->header.is_leaf) {
		return find_min_leaf(ba, node);
	} else {
		return find_min_node(ba, node);
	}
}

KeyPair find_max_node(BufferAllocator& ba, BTNode* node) {
	if (node->header.count == 0) return KeyPair{};

	auto max_child = node->pairs[node->header.count-1];
	return find_max_btree(ba, max_child.value);
}
KeyPair find_min_node(BufferAllocator& ba, BTNode* node){
	if (node->header.count == 0) return KeyPair{};

	auto min_child = node->pairs[0];
	return find_min_btree(ba, min_child.value);
}

DeletePropagation delete_btree(BufferAllocator& ba, std::unordered_set<BlockID>& freed, BlockID id, KeyId key) {
	auto node_raw = ba.load(id);
	auto node = (BTNode*)node_raw.data();

	auto result = node->header.is_leaf
		? delete_leaf(ba, freed, node, key)
		: delete_node(ba, freed, node, key);

	if (result.did_modify) {
		freed.insert(id);
	} 

	return result;
}

DeletePropagation delete_leaf(BufferAllocator& ba, std::unordered_set<BlockID>& free, BTNode* node, KeyId key) {

	// check if node actually contains the key
	bool found = false;
	BlockID deleted_value = 0;
	for(size_t i = 0; i < node->header.count; i++) {
		if (node->pairs[i].key == key) {
			found = true;
			deleted_value = node->pairs[i].value;
			break;
		}
	}
	if (!found) {
		return DeletePropagation { };
	}

	// Copy everything (except deleted key) to a new leaf.
	auto new_leaf_raw = new_empty_leaf(ba);
	auto new_leaf = (BTNode*)new_leaf_raw.data();

	size_t j = 0;
	for (size_t i = 0; i < node->header.count; i++) {
		if (node->pairs[i].key == key) continue;
		new_leaf->pairs[j] = node->pairs[i];
		j++;
		new_leaf->header.count++;
	}

	new_leaf_raw.set_dirty();

	return DeletePropagation {
		.did_modify = true,
		.deleted_value = deleted_value,
		.new_child = new_leaf_raw,
	};
}

DeletePropagation delete_merge(BufferAllocator& ba,
		std::unordered_set<BlockID>& freed,
		BTNode* root, BTNode* left, BTNode* right,
		size_t left_idx, size_t right_idx,
		BlockID deleted_value) {

	auto right_key = root->pairs[right_idx].key;
	auto left_key = root->pairs[left_idx].key;
	bool are_leaves = left->header.is_leaf;

	auto new_node_raw = are_leaves ? new_empty_leaf(ba) : new_empty_node(ba);
	auto new_node = (BTNode*)new_node_raw.data();

	for (size_t i = 0; i < left->header.count; i++, new_node->header.count++) {
		new_node->pairs[new_node->header.count] = left->pairs[i];
	}
	if (!are_leaves && new_node->header.count > 0) {
		new_node->pairs[new_node->header.count-1].key = left_key;
	}
	for (size_t i = 0; i < right->header.count; i++, new_node->header.count++) {
		new_node->pairs[new_node->header.count] = right->pairs[i];
	}

	// create new root
	auto new_root_raw = new_empty_node(ba);
	auto new_root = (BTNode*)new_root_raw.data();

	for (size_t i = 0; i < root->header.count; i++) {
		size_t j = new_root->header.count;
		if (i == right_idx) continue;
		else if (i == left_idx) {
			new_root->pairs[j].key = right_key;
			new_root->pairs[j].value = new_node_raw.id();
			new_root->header.count++;
		} else {
			new_root->pairs[j] = root->pairs[i];
			new_root->header.count++;
		}
	}

	new_node_raw.set_dirty();
	new_root_raw.set_dirty();

	// mark as free
	freed.insert(root->pairs[left_idx].value);
	freed.insert(root->pairs[right_idx].value);

	return DeletePropagation {
		.did_modify = true,
		.deleted_value = deleted_value,
		.new_child = new_root_raw,
	};
}

DeletePropagation move_from_right(BufferAllocator& ba,
		std::unordered_set<BlockID>& freed,
		BTNode* root, BTNode* node, BTNode* right,
		int node_idx, int right_idx,
		BlockID deleted_value) {

	auto right_key = root->pairs[right_idx].key;
	auto node_key = root->pairs[node_idx].key;
	bool are_leaves = right->header.is_leaf;

	// copy to the new node
	auto new_node_raw = clone_node(ba, node);
	auto new_node = (BTNode*)new_node_raw.data();
	new_node->pairs[node->header.count].key = right->pairs[0].key;
	new_node->pairs[node->header.count].value = right->pairs[0].value;
	new_node->header.count++;
	if (!are_leaves) {
		new_node->pairs[new_node->header.count-2].key = node_key;
		new_node->pairs[new_node->header.count-1].key = MAX_KEY_ID;
	}

	// shift right to the left.
	auto new_right_raw = are_leaves ? new_empty_leaf(ba) : new_empty_node(ba);
	auto new_right = (BTNode*)new_right_raw.data();
	for (size_t i = 0; i < right->header.count-1; i++) {
		new_right->pairs[i] = right->pairs[i+1];
		new_right->header.count++;
	}

	new_right_raw.set_dirty();
	new_node_raw.set_dirty();
	
	// update the parent node
	auto new_root_raw = clone_node(ba, root);
	auto new_root = (BTNode*)new_root_raw.data();
	auto new_max = find_min_btree(ba, new_right_raw.id());
	new_root->pairs[node_idx].key = new_max.key;
	new_root->pairs[node_idx].value = new_node_raw.id();
	new_root->pairs[right_idx].key = right_key;
	new_root->pairs[right_idx].value = new_right_raw.id();

	new_root_raw.set_dirty();

	// mark as free
	freed.insert(root->pairs[right_idx].value);
	freed.insert(root->pairs[node_idx].value);

	return DeletePropagation {
		.did_modify = true,
		.deleted_value = deleted_value,
		.new_child = new_root_raw,
	};
}

DeletePropagation move_from_left(BufferAllocator& ba,
		std::unordered_set<BlockID>& freed,
		BTNode* root, BTNode* left, BTNode* node,
		int left_idx, int node_idx,
		BlockID deleted_value) {

	auto left_key = root->pairs[left_idx].key;
	auto node_key = root->pairs[node_idx].key;
	bool are_leaves = left->header.is_leaf;

	// copy to the new node
	auto new_node_raw = are_leaves ? new_empty_leaf(ba) : new_empty_node(ba);
	auto new_node = (BTNode*)new_node_raw.data();
	
	new_node->pairs[0] = left->pairs[left->header.count-1];
	if (!are_leaves) {
		new_node->pairs[0].key = left_key;
	}
	new_node->header.count++;
	for(size_t i = 0; i < node->header.count; i++) {
		new_node->pairs[1+i] = node->pairs[i];
		new_node->header.count++;
	}

	// adjust the left node
	auto new_left_raw = clone_node(ba, left);
	auto new_left = (BTNode*)new_left_raw.data();

	new_left->header.count--;
	new_left->pairs[new_left->header.count] = KeyPair{.key=MAX_KEY_ID};
	if (!are_leaves) {
		new_left->pairs[new_left->header.count-1].key = MAX_KEY_ID;
	}
	
	new_left_raw.set_dirty();
	new_node_raw.set_dirty();

	// update the parent node
	auto new_root_raw = clone_node(ba, root);
	auto new_root = (BTNode*)new_root_raw.data();
	auto new_max = find_min_btree(ba, new_node_raw.id());
	new_root->pairs[left_idx].key = new_max.key;
	new_root->pairs[left_idx].value = new_left_raw.id();
	new_root->pairs[node_idx].key = node_key;
	new_root->pairs[node_idx].value = new_node_raw.id();

	new_root_raw.set_dirty();

	// mark as free
	freed.insert(root->pairs[left_idx].value);
	freed.insert(root->pairs[node_idx].value);

	return DeletePropagation {
		.did_modify = true,
		.deleted_value = deleted_value,
		.new_child = new_root_raw,
	};
}

DeletePropagation delete_node(BufferAllocator& ba, std::unordered_set<BlockID>& free, BTNode* node, KeyId key) {

	size_t idx = 0;
	for (; idx < node->header.count; idx++) {
		if (key < node->pairs[idx].key) {
			break;
		}
	}

	auto child = node->pairs[idx].value;
	auto propagation = delete_btree(ba, free, child, key);

	if (!propagation.did_modify) {
		return propagation;
	}

	// There a few cases we need to handle 
	// 1) the child has enough entries, so just update entry
	// 2) not enough entries but the left node can share.
	// 3) not enough entries but the right node can share.
	// 4) not enough entries but can combine with left.
	// 5) not enough entries but can comine with right.
	
	auto new_child_raw = propagation.new_child;
	auto new_child = (BTNode*)new_child_raw.data();

	// 1)
	if (new_child->enough_entries()) {
		auto new_node_raw = clone_node(ba, node);
		auto new_node = (BTNode*)new_node_raw.data();
		new_node->pairs[idx].value = new_child_raw.id();
		new_node_raw.set_dirty();
		return DeletePropagation {
			.did_modify = true,
			.deleted_value = propagation.deleted_value,
			.new_child = new_node_raw,
		};
	}

	int left_idx = idx - 1;
	size_t right_idx = idx + 1;

	// only left neighbour (2,4)
	if (right_idx >= node->header.count) {
		auto left_node_raw = ba.load(node->pairs[left_idx].value);
		auto left_node = (BTNode*)left_node_raw.data();
		// 2
		if (left_node->can_share_entry())  {
			return move_from_left(ba, free, 
					node, left_node, new_child, 
					left_idx, idx, 
					propagation.deleted_value);
		}
		// 4
		return delete_merge(ba, free,
				node, left_node, new_child,
				left_idx, idx,
				propagation.deleted_value);
	}
	// only right neighbour (3,5)
	else if (left_idx < 0) {
		auto right_node_raw = ba.load(node->pairs[right_idx].value);
		auto right_node = (BTNode*)right_node_raw.data();
		// 3
		if (right_node->can_share_entry()) {
			return move_from_right(ba, free, 
					node, new_child, right_node, 
					idx, right_idx,
					propagation.deleted_value);
		}
		// 5
		return delete_merge(ba, free,
				node, new_child, right_node,
				idx, right_idx,
				propagation.deleted_value);
	}
	// both neighbours (2,4,3)
	else {
		auto left_node_raw = ba.load(node->pairs[left_idx].value);
		auto left_node = (BTNode*)left_node_raw.data();

		// 2
		if (left_node->can_share_entry()) {
			return move_from_left(ba, free, 
					node, left_node, new_child, 
					left_idx, idx, 
					propagation.deleted_value);
		}

		auto right_node_raw = ba.load(node->pairs[right_idx].value);
		auto right_node = (BTNode*)right_node_raw.data();

		// 3
		if (right_node->can_share_entry()) {
			return move_from_right(ba, free, 
					node, new_child, right_node,
					idx, right_idx,
					propagation.deleted_value);
		}

		// 4
		return delete_merge(ba, free,
				node, left_node, new_child,
				left_idx, idx,
				propagation.deleted_value);

	}

}
