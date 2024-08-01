#pragma once

#include "definitions.h"


void intiate_page_allocator(FreeList& free_list, int total_pages);

Header* allocate_page();
void free_page(BlockID);
