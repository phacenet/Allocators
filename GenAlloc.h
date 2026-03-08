#pragma once

#define WIN32_LEAN_AND_MEAN //reduces amount of windows headers pulled in
#define VC_EXTRALEAN
#include <Windows.h> //required for memoryapi.h
#include <memoryapi.h> //for VirtualAlloc (windows only)
#include "Core.h"
#include <ios>

class halloc
{
	struct Header
	{
		/* 2^63 (9 exabytes) before the 63rd bit is touched, IE it is always free */
		size_t _block_size; //_block_size is the entirety, INCLUDING Header struct, make sure to find free mem enough to accomadate payload + sizeof(Header)
		Header* _prev;
	};

private:
	unsigned char* _buf = nullptr;
	size_t _buf_len;

	static const constexpr size_t _alignment = 2 * sizeof(void*); //minimum size of data

	void _set_free(Header** hdr)
	{
		/* Last bit is 0, 1ULL is just 1 as an unsigned long long (uint64_t) since _block_size is a size_t (uint64_t) */
		//(*hdr)->_block_size &= ~1ULL;
		(*hdr)->_block_size &= ~(1ULL << 63);
	}

	void _set_alloc(Header** hdr)
	{
		/* Last bit is 1 */
		//(*hdr)->_block_size |= 1ULL;
		(*hdr)->_block_size |= (1ULL << 63);
	}

	bool _is_free(Header* hdr)
	{
		/* Check that last bit is 0 */
		//return ((hdr->_block_size & 1ULL) == 0);
		return ((hdr->_block_size & (1ULL << 63)) == 0);
	}

	/* Intentionally not modifying original in getter */
	size_t _get_size(Header* hdr)
	{
		/* Mask out last bit to 0 to return true size */
		//return hdr->_block_size & ~1ULL;
		size_t mask = (1ULL << 63) | 0xFULL;
		return hdr->_block_size & ~mask;
	}

	void _set_padding_(Header** hdr, size_t size, size_t aligned_size)
	{
		size_t padding = aligned_size - size;
		(*hdr)->_block_size |= padding;
	}

	size_t _read_padding_(Header* hdr)
	{
		size_t padding = hdr->_block_size & 0xF;
		return padding;
	}

	void _reset_init_()
	{
		Header* hdr = reinterpret_cast<Header*>(_buf);
		hdr->_block_size = _buf_len; //total size
		hdr->_prev = nullptr;
		_set_free(&hdr);
	}

	bool _in_bounds_(Header* mem)
	{
		unsigned char* mem_seg = reinterpret_cast<unsigned char*>(mem);
		unsigned char* start = _buf;
		unsigned char* end = _buf + _buf_len;
		
		return(start <= mem_seg && mem_seg < end);
	}
	/* Overload for unsigned char*, more semantically correct for direct comparison */
	bool _in_bounds_(unsigned char* mem)
	{
		unsigned char* start = _buf;
		unsigned char* end = _buf + _buf_len;

		return(mem >= start && mem < end);
	}

	Header* _get_next_header_(Header* hdr)
	{
		if (_get_size(hdr) == 0)
			return nullptr;
		return reinterpret_cast<Header*>(reinterpret_cast<unsigned char*>(hdr) + _get_size(hdr));
	}

	/* Already assuming header is in our bounds and non-NULL */
	void coalesce(Header** hdr)
	{
		unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(*hdr);
		Header* prev_hdr = (*hdr)->_prev;
		Header* next_hdr = reinterpret_cast<Header*>(raw_hdr + _get_size(*hdr));

		/* Forward coalesce */
		if (_in_bounds_(next_hdr) && _is_free(next_hdr))
		{
			unsigned char* raw_next = reinterpret_cast<unsigned char*>(next_hdr);
			Header* frthr_hdr = _in_bounds_(next_hdr) ? reinterpret_cast<Header*>(raw_next + _get_size(next_hdr)) : nullptr;

			/* Adjust next's next's prev to combined block */
			if (_in_bounds_(frthr_hdr) && frthr_hdr != nullptr)
				frthr_hdr->_prev = *hdr;

			(*hdr)->_block_size = _get_size(*hdr) + _get_size(next_hdr);
			_set_free(&*hdr);
		}
		/* Backwards coalesce */
		if (_in_bounds_(prev_hdr) && _is_free(prev_hdr))
		{
			next_hdr = reinterpret_cast<Header*>(raw_hdr + _get_size(*hdr));

			/* prev consumes hdr */
			prev_hdr->_block_size = _get_size(prev_hdr) + _get_size(*hdr);
			
			/* Adjust further to point to prev since hdr is being consumed */
			if (_in_bounds_(next_hdr))
				next_hdr->_prev = prev_hdr;

			/* Set final */
			(*hdr) = prev_hdr;
		}
	}

	void forward_coalesce(Header** hdr)
	{
		Header* next_hdr = _get_next_header_(*hdr);

		while (_in_bounds_(next_hdr) && _is_free(next_hdr))
		{
			Header* frthr_hdr = _in_bounds_(next_hdr) ? _get_next_header_(next_hdr) : nullptr;

			/* Adjust next's next's prev to combined block */
			if (_in_bounds_(frthr_hdr) && frthr_hdr != nullptr)
				frthr_hdr->_prev = *hdr;

			(*hdr)->_block_size = _get_size(*hdr) + _get_size(next_hdr);
			next_hdr = frthr_hdr;
		}
		_set_free(&*hdr);
	}

	/* Helpers for debug info */
	/* ========================================================================================= */
	/* O(n) */
	size_t getActiveAllocations()
	{
		size_t allocated_regions = 0;

		unsigned char* ptr = _buf;
		while (_in_bounds_(ptr))
		{
			Header* hdr = reinterpret_cast<Header*>(ptr);
			if (!_is_free(hdr))
				++allocated_regions;
			ptr += _get_size(hdr);
		}
		return allocated_regions;
	}
	/* O(n) */
	size_t getFreeMemoryCount()
	{
		size_t running_sum = 0;

		unsigned char* ptr = _buf;
		while (_in_bounds_(ptr))
		{
			Header* hdr = reinterpret_cast<Header*>(ptr);
			if (_is_free(hdr))
				running_sum += _get_size(hdr);
			ptr += _get_size(hdr);
		}
		return running_sum;
	}
	/* O(n) */
	size_t getLastAllocationSize()
	{
		size_t last_alloc_sz = 0;
		unsigned char* ptr = _buf;
		while (_in_bounds_(ptr))
		{
			Header* hdr = reinterpret_cast<Header*>(ptr);
			if (!_is_free(hdr))
				ptr += _get_size(hdr);
			else
				return last_alloc_sz = _get_size(hdr->_prev);
		}
		return -1;
	}
	/* O(1) */
	size_t getOriginalSizeArg(void* mem)
	{
		unsigned char* raw_mem = reinterpret_cast<unsigned char*>(mem);
		if (!_in_bounds_(raw_mem))
			throw;

		Header* hdr = reinterpret_cast<Header*>(raw_mem - sizeof(Header));
		return _get_size(hdr) - _read_padding_(hdr) - sizeof(Header);
	}
	/* ========================================================================================= */

public:

	/* Ctor */
	halloc(size_t size)
	{
		_buf = reinterpret_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		
		if (_buf == nullptr)
			throw;

		_buf_len = size;
		_reset_init_();
	}

	/* Dtor */
	~halloc()
	{
		VirtualFree(_buf, 0, MEM_RELEASE);
		_buf = nullptr;
	}

	/* Splits region if hdr->block_size is enough to store the sizeof(Header) + alignment */
	void* alloc(size_t size)
	{
		/* Align size on static alignment var (16 btyes on 64x) */
		size_t sz = size % 16 == 0 ? size : (size + halloc::_alignment - 1) & ~(halloc::_alignment - 1);

		/* Find suitable memory segment */
		Header* hdr = reinterpret_cast<Header*>(_buf);
		while (_in_bounds_(hdr) && (_get_size(hdr) < sz + sizeof(Header) || !_is_free(hdr)))
		{
			/* Protection against infinite loop corruption */
			if (_get_size(hdr) < sizeof(Header))
				return nullptr;

			hdr = _get_next_header_(hdr);
		}

		/* No segment found */
		if (!_in_bounds_(hdr))
			return nullptr;

		/* Split logic */
		if (_get_size(hdr) >= sz + (sizeof(Header) + halloc::_alignment) )
		{
			unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(hdr);

			/* Write new header at split */
			Header* split_hdr = reinterpret_cast<Header*>(raw_hdr + sizeof(Header) + sz); //adjusted
			split_hdr->_block_size = _get_size(hdr) - (sizeof(Header) + sz);
			split_hdr->_prev = hdr;

			/* Update hdr's next to be split_block, assuming hdr was not the tail */
			Header* next_hdr = reinterpret_cast<Header*>(raw_hdr + _get_size(hdr));
			/* If hdr was not the tail, update next's prev ptr  */
			if (_in_bounds_(next_hdr))
				next_hdr->_prev = split_hdr;

			hdr->_block_size = sizeof(Header) + sz; //still free
		}

		/* Adjust block size of hdr regardless of split */
		_set_alloc(&hdr); //set first bit (63rd)
		_set_padding_(&hdr, size, sz); //set last 4 bits
		unsigned char* user_ptr = reinterpret_cast<unsigned char*>(hdr);
		user_ptr += sizeof(Header);
		return user_ptr;
	}
	
	void free(void* mem)
	{
		if (mem == nullptr)
			return;

		unsigned char* ptr = reinterpret_cast<unsigned char*>(mem);

		if (!_in_bounds_(ptr))
			return;

		Header* hdr = reinterpret_cast<Header*>(ptr - sizeof(Header));
		_set_free(&hdr);

		/* Only one call - neighbors of merged block were already neighbors of blocks that got absorbed
		   If they were free, they would have already been coalesced previously when those blocks were freed */
		coalesce(&hdr);
	}

	void free_all()
	{
		_reset_init_();
	}

	void* realloc(void* mem, size_t newSize)
	{
		if (mem == nullptr)
			return nullptr;

		unsigned char* ptr = reinterpret_cast<unsigned char*>(mem);

		if (!_in_bounds_(ptr))
			return nullptr;

		size_t sz = newSize % 16 == 0 ? newSize : (newSize + halloc::_alignment - 1) & ~(halloc::_alignment - 1);
		unsigned char* raw_mem = reinterpret_cast<unsigned char*>(mem);
		Header* hdr = reinterpret_cast<Header*>(raw_mem - sizeof(Header));
		unsigned char* raw_hdr = reinterpret_cast<unsigned char*>(hdr);
		/* Shrink */
		bool shrinking = _get_size(hdr) > sz;
		bool enoughSpaceForMinPayloadIfSplit = (_get_size(hdr) - sz - sizeof(Header) >= sizeof(Header) + halloc::_alignment);
		/* Split current block */
		if (shrinking && enoughSpaceForMinPayloadIfSplit)
		{
			Header* next_hdr_bfore = _get_next_header_(hdr);
			Header* split_hdr = reinterpret_cast<Header*>(raw_hdr + sizeof(Header) + sz);
			split_hdr->_block_size = _get_size(hdr) - sz - sizeof(Header); //implicitly set free
			split_hdr->_prev = hdr;
			//_set_free(&split_hdr); //explicit for now

			hdr->_block_size = sz + sizeof(Header);
			_set_alloc(&hdr); //set first bit (63rd)
			_set_padding_(&hdr, newSize, sz); //set last 4 bits

			if (_in_bounds_(next_hdr_bfore))
				next_hdr_bfore->_prev = split_hdr;

			forward_coalesce(&split_hdr);

			unsigned char* user_ptr = reinterpret_cast<unsigned char*>(hdr);
			user_ptr += sizeof(Header);
			return user_ptr;
		}

		/* Grow forwards if next block is free and has enough space, also split new block if extra leftover */
		Header* next_hdr = _get_next_header_(hdr);
		bool expanding = _get_size(hdr) < sz;
		bool enough_space = _in_bounds_(next_hdr) ? (_get_size(next_hdr) + _get_size(hdr)) >= sz + sizeof(Header) : false;
		/* Expand */
		if (expanding && _in_bounds_(next_hdr) && _is_free(next_hdr) && enough_space)
		{
			Header* frthr_hdr = _get_next_header_(next_hdr);
			size_t remainder = (_get_size(hdr) + _get_size(next_hdr)) - (sz + sizeof(Header));
			Header* split_hdr = nullptr;

			/* Split forward block if possible */
			if (remainder >= sizeof(Header) + halloc::_alignment)
			{
				split_hdr = reinterpret_cast<Header*>(raw_hdr + sizeof(Header) + sz);
				split_hdr->_block_size = remainder; //implicitly set free
				split_hdr->_prev = hdr;
				hdr->_block_size = sz + sizeof(Header);
			}
			/* No split, consume whole next */
			else
				/* getOriginalSize is slightly off, no harm for memcpy, will just get some extra data. Alternative is permanent unusable fragmented region */
				hdr->_block_size = _get_size(hdr) + _get_size(next_hdr);

			/* Adjust next's next's prev to combined block */
			if (_in_bounds_(frthr_hdr) && frthr_hdr != nullptr)
				frthr_hdr->_prev = (split_hdr != nullptr) ? split_hdr : hdr;

			_set_alloc(&hdr);
			_set_padding_(&hdr, newSize, sz);

			if (split_hdr != nullptr)
				forward_coalesce(&split_hdr);

			unsigned char* user_ptr = reinterpret_cast<unsigned char*>(hdr);
			user_ptr += sizeof(Header);
			return user_ptr;
		}

		/* Allocate new block and free old */
		else
		{
			void* new_ptr = alloc(sz);

			if (new_ptr == nullptr)
				return nullptr;

			/* Only copy REQUESTED bytes */
			size_t original_request_size = getOriginalSizeArg(mem);
			size_t new_size_request = newSize;
			/* Minimum of original requests */
			size_t copy_data = original_request_size < new_size_request ? original_request_size : new_size_request;
			unsigned char* old_data = raw_hdr + sizeof(Header);
			unsigned char* new_data = reinterpret_cast<unsigned char*>(new_ptr); //no + sizeof(Header) because alloc returns start of user data

			memcpy(new_data, old_data, copy_data);

			free(mem);
			
			Header* new_hdr = reinterpret_cast<Header*>(reinterpret_cast<unsigned char*>(new_ptr) - sizeof(Header));
			_set_alloc(&new_hdr);
			_set_padding_(&new_hdr, newSize, sz);
			return new_ptr;
		}
	}

	/* Add a function to visually walk through each block */
	void walk_arena()
	{
		unsigned char* ptr = _buf;
		size_t i = 0;
		while (_in_bounds_(ptr))
		{
			Header* hdr = reinterpret_cast<Header*>(ptr);
			
			std::cout << "---------------------------------------------------------------\n";
			std::cout << "Block " << i << ": ";

			if (!_is_free(hdr))
				std::cout << "allocated";
			else
				std::cout << "free";

			std::cout << ", with current size: " << _get_size(hdr) << ", and padded " << _read_padding_(hdr) << " bytes\n";
			std::cout << "---------------------------------------------------------------\n";

			ptr += _get_size(hdr);
			++i;
		}
	}

	/* Overload for walk arena, debug flag */
	struct debug_t {};
	inline static constexpr debug_t DEBUG{};
	void walk_arena(debug_t)
	{
		unsigned char* ptr = _buf;
		size_t i = 0;
		while (_in_bounds_(ptr))
		{
			Header* hdr = reinterpret_cast<Header*>(ptr);
			unsigned char* user_ptr = reinterpret_cast<unsigned char*>(ptr + sizeof(Header));

			std::cout << "---------------------------------------------------------------\n";
			std::cout << "Block " << i << ": ";

			if (!_is_free(hdr))
				std::cout << "allocated";
			else
				std::cout << "free";

			std::cout << ", with current size: " << _get_size(hdr) << ", and padded " << _read_padding_(hdr) << " bytes\n";

			if (!_is_free(hdr))
			{
				std::cout << "Original passed size arg was : " << getOriginalSizeArg(user_ptr) << " bytes\n";
				std::cout << getOriginalSizeArg(user_ptr) << " + " << _read_padding_(hdr) << " + " << halloc::_alignment << " = "
					<< getOriginalSizeArg(user_ptr) + _read_padding_(hdr) + halloc::_alignment << "\n";
			}
			std::cout << "---------------------------------------------------------------\n";

			ptr += _get_size(hdr);
			++i;
		}
	}


	void printDebug(void* mem)
	{
		unsigned char* raw_mem = reinterpret_cast<unsigned char*>(mem);
		Header* hdr = reinterpret_cast<Header*>(raw_mem - sizeof(Header));

		std::cout << "Original passed size arg was: " << getOriginalSizeArg(mem) << " bytes\n";
		std::cout << "Padding used to align on " << halloc::_alignment << " (2 * sizeof(void*)) was " << _read_padding_(hdr) << " bytes\n";
		std::cout << "Size of header is " << sizeof(Header) << " bytes\n";
		std::cout << getOriginalSizeArg(mem) << " + " << _read_padding_(hdr) << " + " << halloc::_alignment << " = "
			<< getOriginalSizeArg(mem) + _read_padding_(hdr) + halloc::_alignment << "\n";
		std::cout << "Actual allocated region was " << _get_size(hdr) << " bytes.\n";
		std::cout << "Active memory chunks: " << getActiveAllocations() << "\n";
		std::cout << "Remaining free memory is " << getFreeMemoryCount() << " bytes.\n";
	}

	/* No copying or moving */
	halloc() = delete;
	halloc(const halloc& other) = delete;
	halloc operator=(const halloc& other) = delete;
	halloc(const halloc&& other) = delete;
	halloc& operator= (const halloc&& other) = delete;
};