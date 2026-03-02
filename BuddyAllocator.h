#pragma once

#include "Core.h"

class BuddyAllocator
{
	/* Never grab block size with hdr->_block_size, last bit stores a free or allocated flag. Use helper _get_size instead */
	/* Prevents padding from upping struct size from 9->16 bytes, further increasing _min_block_size to 32 rather than the current 16 */
	struct Header
	{
		size_t _block_size = 0;
	};

private:
	unsigned char* _buf = nullptr;
	size_t _buf_len = 0;

	/* Atleast sizeof(Header) bytes of usable space rounded to a power of 2 */
	const size_t _min_block_size = std::bit_ceil(sizeof(Header) * 2);

	Header* get_next_hdr(Header* hdr)
	{
		return reinterpret_cast<Header*>(reinterpret_cast<unsigned char*>(hdr) + _get_size(hdr));
	}

	bool in_bounds(Header* header)
	{
		unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(header);
		return (raw_hdr >= _buf && (raw_hdr < (_buf + _buf_len)));
	};

	void _set_free(Header** hdr)
	{
		/* Last bit is 0, 1ULL is just 1 as an unsigned long long (uint64_t) since _block_size is a size_t (uint64_t) */
		(*hdr)->_block_size &= ~1ULL;
	}

	void _set_alloc(Header** hdr)
	{
		/* Last bit is 1 */
		(*hdr)->_block_size |= 1ULL;
	}

	bool _is_free(Header* hdr)
	{
		return ( (hdr->_block_size & 1ULL) == 0);
	}

	/* Intentionally not modifying original in getter */
	size_t _get_size(Header* hdr)
	{
		/* Mask out last bit to 0 */
		return hdr->_block_size & ~1ULL;
	}

	void _reset_()
	{
		Header* hdr = reinterpret_cast<Header*>(_buf);
		hdr->_block_size = _buf_len;
		/* Set free AFTER setting block size since block_size now stores state as well */
		_set_free(&hdr);
	}

	/* Double ptr to modify the original ptr */
	bool coalesce(Header** hdr)
	{
		bool combined_blocks = false;

		unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(*hdr);
		size_t hdr_offset = raw_hdr - _buf;
		Header* buddy = reinterpret_cast<Header*>(_buf + (hdr_offset ^ _get_size(*hdr)));

		if (in_bounds(buddy) == false)
			return false;

		if (_is_free(buddy) == true && _get_size(buddy) == _get_size(*hdr) )
		{
			size_t hdr_size = _get_size(*hdr);
			size_t buddy_size = _get_size(buddy);

			if (*hdr < buddy)
			{
				(*hdr)->_block_size = hdr_size + buddy_size;
				_set_free(hdr);
			}
			else
			{
				buddy->_block_size = hdr_size + buddy_size;
				_set_free(&buddy);
				*hdr = buddy;
			}

			combined_blocks = true;
		}

		return combined_blocks;
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
		while (in_bounds(hdr) == true && (_is_free(hdr) == false || _get_size(hdr) < sz))
			hdr = get_next_hdr(hdr);

		/* No valid block was found */
		if (in_bounds(hdr) == false)
			return nullptr;

		/* Recursive split logic */
		while(_get_size(hdr) / 2 >= sz)
		{
			size_t curr_size = _get_size(hdr);
			unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(hdr);

			Header* split_block = reinterpret_cast<Header*>(raw_hdr + (curr_size / 2));
			split_block->_block_size = curr_size / 2;
			_set_free(&split_block);

			hdr->_block_size = curr_size / 2;
		}

		_set_alloc(&hdr);
		unsigned char* user_ptr = reinterpret_cast<unsigned char*>(hdr) + sizeof(Header);
		memset(user_ptr, 0, _get_size(hdr) - sizeof(Header));
		//std::cout << "Actual used memory was " << _get_size(hdr) << "\n";
		return user_ptr;
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
		_set_free(&hdr);

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

	size_t getRemainingMemory()
	{
		uintptr_t start = reinterpret_cast<uintptr_t>(_buf);
		uintptr_t end = start + static_cast<uintptr_t>(_buf_len);
		unsigned char* raw_hdr = _buf;
		Header* hdr = reinterpret_cast<Header*>(raw_hdr);
		
		size_t allocated_memory = 0;

		/* Walk through every header and calc a walking sum if it is allocated */
		while (raw_hdr < (_buf + _buf_len))
		{
			if(_is_free(hdr) == false)
				allocated_memory += _get_size(hdr);

			raw_hdr = reinterpret_cast<unsigned char*>(get_next_hdr(hdr));
			hdr = reinterpret_cast<Header*>(raw_hdr);
		}

		return (_buf_len - allocated_memory);
	}

	void printRemainingMemory()
	{
		std::cout << "Free memory remaining: " << getRemainingMemory() << "\n";
	}

	/* No default ctor */
	BuddyAllocator() = delete;
	/* No copying or moving */
	BuddyAllocator(const BuddyAllocator& other) = delete;
	BuddyAllocator operator= (const BuddyAllocator& other) = delete;
	BuddyAllocator(const BuddyAllocator&& other) = delete;
	BuddyAllocator& operator= (BuddyAllocator&& other) = delete;
};