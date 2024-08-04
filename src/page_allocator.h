#pragma once

#include "buffer_allocator.h"
#include "definitions.h"


void initiate_page_allocator(BufferAllocator& ba, int total_pages);

BufferPointer allocate_page(BufferAllocator& ba);
void free_page(BufferAllocator& ba, BlockID);
