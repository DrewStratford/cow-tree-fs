#pragma once

#include <vector>
#include <optional>

#include "buffer_allocator.h"
#include "definitions.h"

void create_file_system(BufferAllocator& ba, size_t total_pages);

std::optional<BlockID> insert(BufferAllocator& ba, KeyId key, BlockID value);
std::optional<BlockID> lookup(BufferAllocator& ba, KeyId key);
std::optional<BlockID> remove(BufferAllocator& ba, KeyId key);

void create_root_directory(BufferAllocator& ba);
std::optional<KeyId> add_directory(BufferAllocator& ba, KeyId parent_key, char* name);
std::optional<KeyId> add_file(BufferAllocator& ba, KeyId parent_key, char* name);
void list_directory(BufferAllocator& ba, KeyId key);
void write_file(BufferAllocator& ba, KeyId key, char* data, size_t len, size_t pos);
void read_file(BufferAllocator& ba, KeyId key);
void append_file(char* data, size_t len, size_t pos);
enum FSType { Unknown, SmallDir, SmallFile};
struct [[gnu::packed]] FSHeader {
	KeyId key { 0 };
	BlockID block { 0 };
	FSType type { Unknown };
};

struct [[gnu::packed]] DirEntry {
	KeyId data { 0 };
	FSType type { Unknown };
	size_t name_len { 0 };
	char name[];
};

const size_t MAX_DIR_DATA = PAGE_SIZE - sizeof(FSHeader) - sizeof(size_t);
struct [[gnu::packed]] Directory {
	FSHeader header;
	size_t size { 0 };
	char data[MAX_DIR_DATA];

	void insert_file(char* name, FSType type, KeyId block);
	void list_contents();
	std::optional<KeyId> lookup_file(char* name);
};

const size_t MAX_FILE_DATA = PAGE_SIZE - sizeof(FSHeader) - sizeof(size_t);
struct [[gnu::packed]] File {
	FSHeader header;
	size_t size { 0 };
	char data[MAX_FILE_DATA];

	void write(char* data, size_t len, size_t pos);
	void append(char* data, size_t len);
	void read();
};
