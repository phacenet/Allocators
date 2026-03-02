#pragma once

#include "Core.h"

class LinearArena
{
private:
	unsigned char* _buf = nullptr;
	size_t _buf_len = 0;
	size_t _cur_offset = 0;

public:
	/* Parameterized ctor, set backend _buf to _size passed by user and 0 mem */
	LinearArena(size_t size)
		: _buf_len(size)
	{
		_buf = static_cast<unsigned char*>(malloc(_buf_len));
		if(_buf != nullptr)
			memset(_buf, 0, _buf_len);
	}

	~LinearArena()
	{
		free(_buf);
		_buf = nullptr;
	}

	void* alloc(size_t size, size_t alignment = (2 * sizeof(void*)) ) //default alignment is power of 2
	{
		uintptr_t sz = static_cast<uintptr_t>(size);
		/* Ensure alignment is a power of 2 */
		uintptr_t algnmnt = static_cast<uintptr_t>(std::bit_ceil(alignment));
		/* Guarantee offset alignment with algnmnt */
		uintptr_t aligned_offset = (_cur_offset + algnmnt - 1) & ~(algnmnt - 1);

		/* Not enough memory available to divvy out request */
		if (aligned_offset + sz > _buf_len)
			return nullptr;

		/* Otherwise fulfill request */
		void* ptr = &_buf[aligned_offset];
		_cur_offset = aligned_offset + sz;
		/* Zero mem */
		memset(ptr, 0, sz);

		return ptr;
	}

	void free_all()
	{
		_cur_offset = 0;
	}

	/* No default ctor */
	LinearArena() = delete;
	/* No copying or moving */
	LinearArena(const LinearArena& other) = delete;
	LinearArena operator= (const LinearArena& other) = delete;
	LinearArena(const LinearArena&& other) = delete;
	LinearArena& operator= (LinearArena&& other) = delete;
};