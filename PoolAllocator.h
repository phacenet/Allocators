#pragma once

#include "Core.h"

class PoolAllocator
{
	struct Pool_Free_Node
	{
		Pool_Free_Node* next;
	};

private:
	unsigned char* _buf = nullptr;
	size_t _buf_len = 0;
	size_t _chunk_sz = 0;

	Pool_Free_Node* head = nullptr;

	/* Helper for resetting to default state */
	void _reset_all_chunks()
	{
		uint32_t num_chunks = _buf_len / _chunk_sz;
		for (uint32_t i{ 0 }; i < num_chunks - 1; ++i)
		{
			Pool_Free_Node* hdr = reinterpret_cast<Pool_Free_Node*>(&_buf[(i * _chunk_sz)]);
			hdr->next = reinterpret_cast<Pool_Free_Node*>(&_buf[((i + 1) * _chunk_sz)]);
		}

		/* Assign last chunk's next to nullptr */
		Pool_Free_Node* hdr = reinterpret_cast<Pool_Free_Node*>(&_buf[((num_chunks - 1) * _chunk_sz)]);
		hdr->next = nullptr;

		/* Finalize */
		head = reinterpret_cast<Pool_Free_Node*>(_buf);
	}

public:

	/* Parameterized ctor - initialize the pool with a pre-allocated memory buffer
	* Header lives in chunk until passed to the user, then it is zeroed (in pool_alloc)
	*/
	PoolAllocator(size_t buffer_length, size_t chunk_size, size_t chunk_alignment = (2 * sizeof(void*)) )
	{
		if (buffer_length == 0 || chunk_size == 0)
			throw;
		else if (buffer_length <= chunk_size)
			throw;
		/* Chunk Size needs to be rounded up to atleast be able to store Pool_Free_Node */
		else if (chunk_size < sizeof(Pool_Free_Node))
			chunk_size = sizeof(Pool_Free_Node);

		/* Align on power of 2 */
		uintptr_t chnk_algnmnt = static_cast<uintptr_t>(std::bit_ceil(chunk_alignment));
		/* Align on a multiple of chnk_algnmnt */
		uintptr_t chnk_sz = static_cast<uintptr_t>((chunk_size + chnk_algnmnt - 1) & ~(chnk_algnmnt - 1));

		/* Allocate the Arena */
		_buf = static_cast<unsigned char*>(malloc(buffer_length));
		if (_buf == nullptr)
			throw;

		/* BEFORE _reset_all_chunks() so it uses the right params */
		_buf_len = buffer_length;
		_chunk_sz = chnk_sz; //size_t = uintptr_t, check for warning

		_reset_all_chunks();
	}

	~PoolAllocator()
	{
		free(_buf);
		_buf = nullptr;
	}

	/* PoolAlloc - hands out a chunk and removes free_node head */
	void* pool_alloc()
	{
		/* No more chunks available */
		if (head == nullptr)
			return nullptr;

		/* Save current head */
		void* ptr = head;
		/* Advance head to next address */
		head = head->next;

		/* Zero entire memory region once passed to the user */
		memset(ptr, 0, _chunk_sz);
		return ptr;
	}

	/* Pool Free - pushes on the freed chunk as the head of the free list */
	void pool_free(void* chunk)
	{
		if (chunk == nullptr)
			return;

		uintptr_t start = reinterpret_cast<uintptr_t>(_buf);
		uintptr_t end = start + static_cast<uintptr_t>(_buf_len);
		uintptr_t curr_address = reinterpret_cast<uintptr_t>(chunk);

		/* chunk passed is not in the arena */
		if (!(start <= curr_address && curr_address < end))
		{
			assert(0 && "Out of bounds memory address passed to stack_resize");
			return;
		}

		Pool_Free_Node* hdr = reinterpret_cast<Pool_Free_Node*>(static_cast<unsigned char*>(chunk));
		hdr->next = head;
		head = hdr;
	}

	/* Pool Free All - pushes every chunk in the pool onto the free list */
	void pool_free_all()
	{
		_reset_all_chunks();
	}


	/* No default ctor */
	PoolAllocator() = delete;
	/* No copying or moving */
	PoolAllocator(const PoolAllocator& other) = delete;
	PoolAllocator operator= (const PoolAllocator& other) = delete;
	PoolAllocator(const PoolAllocator&& other) = delete;
	PoolAllocator& operator= (PoolAllocator&& other) = delete;
};