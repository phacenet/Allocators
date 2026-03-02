#pragma once

#include "Core.h"
#include <iostream>

class FreeListAllocator
{

	struct HeaderFree
	{
		size_t block_size = 0;
		HeaderFree* next = nullptr;
	};

	struct HeaderAllocated
	{
		size_t block_size;
	};

private:
	unsigned char* _buf = nullptr;
	size_t _buf_len = 0;

	HeaderFree* head = nullptr;

	/* Helper for ctor and free_all */
	void _reset_list()
	{
		HeaderFree* hdr = reinterpret_cast<HeaderFree*>(_buf);
		hdr->block_size = _buf_len - sizeof(HeaderFree);
		hdr->next = nullptr;
		head = hdr;
		std::cout << "Value of _buf in _reset_list() after it is set: " << _buf << "\n";
		std::cout << "Value of head in _reset_list() after it is set: " << head << "\n";
	}

public:

	FreeListAllocator(size_t bufferLength)
	{
		if (bufferLength == 0)
			throw;

		/* Allocate the Arena */
		_buf = static_cast<unsigned char*>(malloc(bufferLength));
		if (_buf == nullptr)
			throw;

		/* Finalize */
		_buf_len = bufferLength;
		_reset_list();
	}
	
	~FreeListAllocator()
	{
		free(_buf);
		_buf = nullptr;
	}

	void* free_list_alloc(size_t size, size_t alignment = (2 * sizeof(void*)))
	{
		uintptr_t algnmnt = static_cast<uintptr_t>(std::bit_ceil(alignment));
		uintptr_t sz = static_cast<uintptr_t>((size + algnmnt - 1) & ~(algnmnt - 1));

		HeaderFree* hdr = head;
		HeaderFree* hdr_prev = nullptr; //stays null if there 1 or less free blocks (head)

		/* Search the linked list for a block where the data can fit */
		while (hdr != nullptr)
		{
			if (hdr->block_size < sz)
			{
				hdr_prev = hdr;
				hdr = hdr->next;
			}
			else
				break;
		}

		if (hdr == nullptr)
			return nullptr;

		/* Region is NOT split if the extra CANNOT store a HeaderStruct and alignment worth of bytes*/
		size_t difference = hdr->block_size - sz;
		if (difference < (sizeof(HeaderFree) + algnmnt))
		{
			/* No split, so use full block size of memory */
			sz = hdr->block_size;
			
			/* Remove region from free list */
			if (hdr_prev != nullptr)
				hdr_prev->next = hdr->next;
			else
				head = hdr->next;
		}

		/* The region IS split */
		else
		{
			/* Write a free struct at the start of the split */
			unsigned char* segment = reinterpret_cast<unsigned char*>(hdr);
			HeaderFree* split_block = reinterpret_cast<HeaderFree*>(segment + sizeof(HeaderAllocated) + sz);

			split_block->block_size = difference;
			split_block->next = hdr->next;

			/* More than head as a free block */
			if (hdr_prev != nullptr)
				hdr_prev->next = split_block;
			else
				head = split_block;
		}

		/* Return region */
		HeaderAllocated* ptr = reinterpret_cast<HeaderAllocated*>(hdr);
		ptr->block_size = sz;

		unsigned char* user_ptr = reinterpret_cast<unsigned char*>(ptr) + sizeof(HeaderAllocated);
		memset(user_ptr, 0, sz);
		return (user_ptr);
	}

	void free_list_free(void* mem_block)
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

		/* NO reading from hdr here since it is cast to a HeaderFree before it is actually freed. Only for address comparison */
		HeaderFree* hdr = reinterpret_cast<HeaderFree*>(static_cast<unsigned char*>(mem_block) - sizeof(HeaderAllocated));
		HeaderFree* ptr = head;
		HeaderFree* ptr_prev = nullptr; //stays null if there 1 or less free blocks (head)

		/* As not to enforce LIFO, iterate until the next free address before hdr (mem_block header) */
		while (ptr != nullptr && ptr < hdr)
		{
			ptr_prev = ptr;
			ptr = ptr->next;
		}

		HeaderAllocated* chunk = reinterpret_cast<HeaderAllocated*>(static_cast<unsigned char*>(mem_block) - sizeof(HeaderAllocated));
		size_t mem_block_size = chunk->block_size;
		hdr->block_size = mem_block_size;

		/* Insert hdr between ptr_prev and prev */
		hdr->next = ptr;
		if (ptr_prev != nullptr)
			ptr_prev->next = hdr;
		else
			head = hdr;

		/* ptr_prev CAN be nullptr, so additional check for backward case in coalesce */\
		coalesce(ptr_prev, hdr);
	}

	void free_list_free_all()
	{
		_reset_list();
	}

	/* Uses sizeof(HeaderAllocated) for computation of physical end of block due to the fact that allocated size is always 
	* hdr + 8(sizeof(HeaderAllocated)) + sz NOT hdr + 16(sizeof(HeaderFree)) + sz 
	* 8 bytes of data address is just overwritten when the struct is freed and changed to a HeaderFree
	* So physical end is always header + sizeof(HeaderAllocated) + block_size regardless of whether it is allocated or free
	*/
	void coalesce(HeaderFree* prev, HeaderFree* hdr)
	{
		/* Forward Coalesce */
		unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(hdr);

		if ((raw_hdr + sizeof(HeaderAllocated) + hdr->block_size) == reinterpret_cast<unsigned char*>(hdr->next))
		{
			hdr->block_size += (hdr->next->block_size + sizeof(HeaderAllocated));
			hdr->next = hdr->next->next;
		}

		/* Backwards coalesce */
		unsigned char* raw_prev = reinterpret_cast<unsigned char*>(prev);
		if (prev != nullptr && (raw_prev + sizeof(HeaderAllocated) + prev->block_size) == raw_hdr)
		{
			prev->block_size += (hdr->block_size + sizeof(HeaderAllocated));
			prev->next = hdr->next;
		}
	}

	/* No default ctor */
	FreeListAllocator() = delete;
	/* No copying or moving */
	FreeListAllocator(const FreeListAllocator& other) = delete;
	FreeListAllocator operator= (const FreeListAllocator& other) = delete;
	FreeListAllocator(const FreeListAllocator&& other) = delete;
	FreeListAllocator& operator= (FreeListAllocator&& other) = delete;
};