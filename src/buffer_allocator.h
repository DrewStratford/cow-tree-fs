#pragma once

#include <unordered_map>
#include <cstddef>
#include <cstdio>

#include "definitions.h"

struct BufferTag {
	int index { 0 };
	BufferTag* next { nullptr };
	int references { 0 };
	bool dirty { false };
	size_t offset { 0 };
};

class BufferPointer;


class BufferAllocator {
	private:
		
		FILE* m_file { nullptr }; 
		BufferTag* m_free { nullptr };
		size_t m_capacity { 0 };
		BufferTag* m_tags { nullptr };
		char* m_buffers { nullptr };

		std::unordered_map<size_t, size_t> m_offset_to_index;

		size_t allocate();
		void unallocate(size_t index);

		void evict(size_t index);

	public:
		BufferAllocator(FILE* file, size_t capacity);

		
		BufferPointer load(size_t offset);
		void flush(size_t index);

		char* get_buffer(size_t index);
		size_t obtain(size_t index);
		void release(size_t index);

		BlockID get_id(size_t index);

		void set_dirty(size_t index);
};

class BufferPointer {
	private:
		BufferAllocator* m_allocator { nullptr };
		size_t m_index { 0 };
		char *m_buffer { nullptr };
	
	public:
		BufferPointer() {};

		BufferPointer(BufferAllocator& ba, int i, char* buf) 
			: m_allocator(&ba), m_index(i), m_buffer(buf) {
				if (m_allocator) m_allocator->obtain(m_index);
			}

		BufferPointer(BufferPointer&& buf) {
			if (m_allocator) {
				m_allocator->release(m_index);
			}
			m_index = buf.m_index;
			m_buffer = buf.m_buffer;
			m_allocator = buf.m_allocator;
			if (m_allocator) m_allocator->obtain(m_index);
		}

		BufferPointer(const BufferPointer& buf) {
			if (m_allocator) {
				m_allocator->release(m_index);
			}
			m_index = buf.m_index;
			m_buffer = buf.m_buffer;
			m_allocator = buf.m_allocator;
			if (m_allocator) m_allocator->obtain(m_index);
		}

		BufferPointer& operator=(const BufferPointer& buf) {
			if (m_allocator) {
				m_allocator->release(m_index);
			}
			m_index = buf.m_index;
			m_buffer = buf.m_buffer;
			m_allocator = buf.m_allocator;
			if (m_allocator) m_allocator->obtain(m_index);

			return *this;
		}

		~BufferPointer() {
			if (m_allocator) {
				m_allocator->release(m_index);
			}
		}

		char* data() {
			return m_buffer;
		}

		BlockID id() {
			if (m_allocator) {
				return m_allocator->get_id(m_index);
			}

			return -1;
		}

		void flush() {
			if (m_allocator) {
				m_allocator->flush(m_index);
			}
		}

		void set_dirty() {
			if (m_allocator) {
				m_allocator->set_dirty(m_index);
			}
		}

		operator bool() {
			return m_allocator != nullptr;
		}

		void write(void* buf, size_t len, size_t offset);

};
