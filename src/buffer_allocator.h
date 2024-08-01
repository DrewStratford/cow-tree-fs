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

		std::unordered_map<size_t, int> m_offset_to_index;

		int allocate();
		void unallocate(int index);

		void evict(int index);

	public:
		BufferAllocator(FILE* file, size_t capacity);

		
		BufferPointer load(size_t offset);
		void flush(int index);

		char* get_buffer(int index);
		int obtain(int index);
		void release(int index);

		void set_dirty(int index);
};

class BufferPointer {
	private:
		int m_index { 0 };
		char *m_buffer { nullptr };
		BufferAllocator* m_allocator { nullptr };
	
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

		void flush() {
			if (m_allocator) {
				m_allocator->flush(m_index);
			}
		}

		operator bool() {
			return m_allocator != nullptr;
		}

		void write(void* buf, size_t len, size_t offset);

};
