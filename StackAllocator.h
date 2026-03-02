#pragma once

#include "Core.h"
#include <iostream>

class StackAllocator
{
private:
	unsigned char* _buf = nullptr;
	size_t _buf_len = 0;
	size_t _cur_offset = 0;

	struct Header
	{
		uint32_t _prev_offset;
		/* Storing previous alignment for comparison in resize */
		size_t _prev_alignment;
	};

public:
	/* Parameterized Ctor */
	StackAllocator(size_t size)
		: _buf_len(size)
	{
		_buf = static_cast<unsigned char*>(malloc(_buf_len));
		if (_buf != nullptr)
			memset(_buf, 0, _buf_len);
	}

	~StackAllocator()
	{
		free(_buf);
		_buf = nullptr;
	}


	/* alloc 
	* stack_alloc increments the offset to indicate the current buffer offset whilst taking into account the allocation header
	*/
	void* stack_alloc(size_t size, size_t alignment = (2 * sizeof(void*)) ) //default alignment as power of 2
	{
		/* Ensure alignment is a power of 2 */
		uintptr_t algnmnt = static_cast<uintptr_t>(std::bit_ceil(alignment));
		/* Guarantee offset alignment with algnmnt */
		uintptr_t aligned_offset = (_cur_offset + algnmnt - 1) & ~(algnmnt - 1);
		/* Make sure size is a multiple of algmnt */
		uintptr_t sz = static_cast<uintptr_t>((size + algnmnt - 1) & ~(algnmnt - 1));

		/* Not enough memory available to divvy out request */
		if (aligned_offset + sz + sizeof(Header) > _buf_len)
			return nullptr;

		/* Write previous offset into the memory segment right before ptr */
		Header* hdr = reinterpret_cast<Header*>(&_buf[aligned_offset]);
		hdr->_prev_offset = _cur_offset;
		hdr->_prev_alignment = alignment;

		/* Otherwise fulfill request */
		void* ptr = &_buf[aligned_offset + sizeof(Header)];
		_cur_offset = aligned_offset + sz + sizeof(Header);
		/* Zero mem */
		memset(ptr, 0, sz);

		return ptr;
	}

	/* free 
	* stack_free frees the memory passed to it and decrements the offset to free that memory
	*/
	void stack_free(void* mem_block)
	{
		if (mem_block == nullptr)
			return;

		Header* hdr = reinterpret_cast<Header*>(static_cast<unsigned char*>(mem_block) - sizeof(Header));
		_cur_offset = hdr->_prev_offset;
	}

	/* resize 
	* stack_resize first checks to see if the allocation being resized was the previously performed allocation 
	* and if so, the same pointer will be returned and the buffer offset is changed. Otherwise, stack_alloc will be called instead.
	*/
	void* stack_resize(void* mem_block, size_t new_size, size_t old_size, size_t new_alignment = (2 * sizeof(void*)))
	{
		/* Bad cases */
		if (mem_block == nullptr)
			return nullptr;
		else if (new_size == 0)
			return nullptr;

		uintptr_t start = reinterpret_cast<uintptr_t>(_buf);
		uintptr_t end = start + static_cast<uintptr_t>(_buf_len);
		uintptr_t curr_address = reinterpret_cast<uintptr_t>(mem_block);

		if (!(start <= curr_address && curr_address < end))
		{
			assert(0 && "Out of bounds memory address passed to stack_resize");
			return nullptr;
		}

		/* Retrieve _prev_offset */
		Header* hdr = reinterpret_cast<Header*>(static_cast<unsigned char*>(mem_block) - sizeof(Header));

		uintptr_t mem_offset = static_cast<uintptr_t>((static_cast<unsigned char*>(mem_block) - _buf));
		uintptr_t old_sz = static_cast<uintptr_t>((old_size + hdr->_prev_alignment - 1) & ~(hdr->_prev_alignment - 1));

		/* It was the last allocation */
		if (mem_offset + old_sz == _cur_offset)
		{
			_cur_offset = sizeof(Header) + hdr->_prev_offset + new_size;
			return mem_block;
		}

		/* It was not the last allocation */
		else
			return stack_alloc(new_size, new_alignment);
	}
	/* free all */
	void free_all()
	{
		_cur_offset = 0;
	}

	/* No default ctor */
	StackAllocator () = delete;
	/* No copying or moving */
	StackAllocator(const StackAllocator& other) = delete;
	StackAllocator operator= (const StackAllocator& other) = delete;
	StackAllocator(const StackAllocator&& other) = delete;
	StackAllocator& operator= (StackAllocator&& other) = delete;

	size_t getCurrentOffset() const
	{
		return _cur_offset;
	}

	size_t getPreviousOffset(void* mem_block) const
	{
		Header* hdr = reinterpret_cast<Header*>(static_cast<unsigned char*>(mem_block) - sizeof(Header));
		return hdr->_prev_offset;
	}

};