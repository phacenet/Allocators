#pragma once

#include "Core.h"

class BuddyAllocator
{

	struct Header
	{
		size_t _block_size = 0;
		bool _is_free = true;
	};

private:
	unsigned char* _buf = nullptr;
	size_t _buf_len = 0;

	/* Atleast sizeof(Header) bytes of usable space rounded to a power of 2 */
	const size_t _min_block_size = std::bit_ceil(sizeof(Header) * 2);

	Header* get_next_hdr(Header* hdr)
	{
		return reinterpret_cast<Header*>(reinterpret_cast<unsigned char*>(hdr) + hdr->_block_size);
	}

	bool in_bounds(Header* header)
	{
		unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(header);
		return (raw_hdr >= _buf && (raw_hdr < (_buf + _buf_len)));
	};

	/* Double ptr to modify the original ptr */
	bool coalesce(Header** hdr)
	{
		bool combined_blocks = false;

		unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(*hdr);
		size_t hdr_offset = raw_hdr - _buf;
		Header* buddy = reinterpret_cast<Header*>(_buf + (hdr_offset ^ (*hdr)->_block_size));

		if (in_bounds(buddy) == false)
			return false;

		if (buddy->_is_free == true)
		{
			if (*hdr < buddy)
				(*hdr)->_block_size += buddy->_block_size;
			else
			{
				buddy->_block_size += (*hdr)->_block_size;
				*hdr = buddy;
			}

			combined_blocks = true;
		}

		return combined_blocks;
	}

	void _reset_()
	{
		Header* hdr = reinterpret_cast<Header*>(_buf);
		hdr->_is_free = true;
		hdr->_block_size = _buf_len;
	}

public:

	/* Ctor */
	BuddyAllocator(size_t bufferLength)
	{
		if (bufferLength == 0 || bufferLength < _min_block_size)
			throw;

		bufferLength = std::bit_ceil(bufferLength);

		_buf = static_cast<unsigned char*>(malloc(bufferLength));
		if (_buf == nullptr)
			throw;

		_buf_len = bufferLength;

		/* Write one header to entire block */
		_reset_();
	}

	/* Dtor */
	~BuddyAllocator()
	{
		free(_buf);
		_buf = nullptr;
	}

	void* buddy_alloc(size_t size)
	{
		size_t sz = size > _min_block_size ? std::bit_ceil(size + sizeof(Header)) : _min_block_size;
		Header* hdr = reinterpret_cast<Header*>(_buf);

		/* Move to next when the current block is NOT sufficient */
		while (in_bounds(hdr) == true && (hdr->_is_free == false || hdr->_block_size < sz))
			hdr = get_next_hdr(hdr);

		/* No valid block was found */
		if (in_bounds(hdr) == false)
		{
			std::cout << "Choosing nullptr branch\n";
			return nullptr;
		}
		/* Recursive split logic */
		while(hdr->_block_size / 2 >= sz)
		{
			unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(hdr);
			Header* split_block = reinterpret_cast<Header*>(raw_hdr + (hdr->_block_size / 2));
			split_block->_block_size = hdr->_block_size / 2;
			split_block->_is_free = true;

			hdr->_block_size /= 2;
		}

		hdr->_is_free = false;
		std::cout << "Actual used memory " << hdr->_block_size << "\n";
		return reinterpret_cast<unsigned char*>(hdr) + sizeof(Header);
	}

	void buddy_free(void* mem_block)
	{
		if (mem_block == nullptr)
			return;

		uintptr_t start = reinterpret_cast<uintptr_t>(_buf);
		uintptr_t end = start + static_cast<uintptr_t>(_buf_len);
		uintptr_t curr_address = reinterpret_cast<uintptr_t>(mem_block);

		/* Block passed is not in the arena */
		if (!(start <= curr_address && curr_address < end))
		{
			assert(0 && "Out of bounds memory address passed to stack_resize");
			return;
		}

		Header* hdr = reinterpret_cast<Header*>(static_cast<unsigned char*>(mem_block) - sizeof(Header));
		hdr->_is_free = true;

		/* Coalesce takes care of entirety */
		while (coalesce(&hdr) == true) {;}
	}

	void buddy_free_all()
	{
		_reset_();
	}

	size_t getMinBlockSize()
	{
		return _min_block_size;
	}

	size_t getHeaderStructSize()
	{
		return sizeof(Header);
	}

	/* No default ctor */
	BuddyAllocator() = delete;
	/* No copying or moving */
	BuddyAllocator(const BuddyAllocator& other) = delete;
	BuddyAllocator operator= (const BuddyAllocator& other) = delete;
	BuddyAllocator(const BuddyAllocator&& other) = delete;
	BuddyAllocator& operator= (BuddyAllocator&& other) = delete;
};