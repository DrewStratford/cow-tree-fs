#pragma once

#include <optional>

#include "buffer_allocator.h"
#include "definitions.h"

void create_file_system(BufferAllocator& ba, size_t total_pages);
void append(BufferAllocator& ba, char* data, size_t len);
void peek(BufferAllocator& ba);
void pop(BufferAllocator& ba);


std::optional<BlockID> insert(BufferAllocator& ba, KeyId key, BlockID value);
BlockID lookup(BufferAllocator& ba, KeyId key);
BlockID remove(BufferAllocator& ba, KeyId key);
