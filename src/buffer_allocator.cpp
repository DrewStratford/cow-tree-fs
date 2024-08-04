#include <cstring>

#include "buffer_allocator.h"




BufferAllocator::BufferAllocator(FILE* file, size_t capacity) : m_file(file), m_capacity(capacity) {
	m_tags = new BufferTag[capacity];
	m_buffers = new char[capacity * PAGE_SIZE];

	// set up pages in a free list
	m_free = &m_tags[0];
	for (int i = 0; i < m_capacity-1; i++) {
		m_tags[i].next = &m_tags[i+1];
	}
	// Set up the index (we could be smarter and use point arith).
	for(int i = 0; i < m_capacity; i++) {
		m_tags[i].index = i;
	}
}

BlockID BufferAllocator::get_id(int index) {
	if (index < 0 || m_capacity <= index)
		return -1;
	auto tag = &m_tags[index];
	return tag->offset;
}

void BufferAllocator::set_dirty(int index) {
	if (index < 0 || m_capacity <= index)
		return;
	auto tag = &m_tags[index];
	tag->dirty = true;
}

int BufferAllocator::allocate() {
	// TODO: handle oom
	if (!m_free) return -1;

	auto tag = m_free;
	m_free = tag->next;
	
	return tag->index;
}

void BufferAllocator::unallocate(int index) {
	if (index < 0 || m_capacity <= index)
		return;

	auto tag = &m_tags[index];
	tag->next = m_free;
	tag->references = 0;
	tag->dirty = false;
	tag->offset = 0;

	m_free = tag;
}

int BufferAllocator::obtain(int index) {
	if (index < 0 || m_capacity <= index)
		return -1;
	auto tag = &m_tags[index];
	tag->references++;
	return index;
}

char* BufferAllocator::get_buffer(int index) {
	if (index < 0 || m_capacity <= index)
		return nullptr;
	return m_buffers + (index * PAGE_SIZE);
}

void BufferAllocator::release(int index) {
	if (index < 0 || m_capacity <= index)
		return;

	auto tag = &m_tags[index];
	tag->references--;
	printf("releasing %d %d\n", index, tag->references);
	if (tag->references <= 0) {
		printf("flushing %d\n", index);
		flush(index);
		unallocate(index);
		m_offset_to_index.erase(tag->offset);
	}
}

BufferPointer BufferAllocator::load(size_t offset) {
	if (m_offset_to_index.count(offset) > 0) {
		auto idx = m_offset_to_index[offset];
		return BufferPointer(*this, idx, get_buffer(idx));
	}

	// TODO: unallocate on failure
	auto idx = allocate();
	if (idx < 0) return BufferPointer();

	auto tag = &m_tags[idx];
	char* buffer = get_buffer(idx);

	tag->offset = offset;

	// TODO: error checking
	fseek(m_file, offset, SEEK_SET);
	fread(buffer, sizeof(char), PAGE_SIZE, m_file);

	m_offset_to_index[offset] = idx;

	return BufferPointer(*this, idx, buffer);
}

void BufferAllocator::flush(int index) {
	if (index < 0 || m_capacity <= index)
		return;

	auto tag = &m_tags[index];
	if (!tag->dirty) return;
	char* buffer = get_buffer(index);

	// TODO: error checking
	fseek(m_file, tag->offset, SEEK_SET);
	fwrite(buffer, sizeof(char), PAGE_SIZE, m_file);

	tag->dirty = false;
}

void BufferPointer::write(void* buf, size_t len, size_t offset) {
	// TODO: enforce maximum
	if (!m_allocator) return;

	char* dest = m_buffer+offset;
	memcpy(dest, buf, len);
	m_allocator->set_dirty(m_index);
}
