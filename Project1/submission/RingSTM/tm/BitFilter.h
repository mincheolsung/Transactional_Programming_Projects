/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/**
 *  This file implements a simple bit filter datatype, with SSE2 optimizations.
 *  The type is templated by size, but has only been tested at a size of 1024
 *  bits.
 */

 /**
 * Usage:
 *
 * BitFilter<1024> sig;
 * sig.add(&var);
 * printf("%d\n", sig.lookup(&var));
 */
 
#ifndef BITFILTER_HPP__
#define BITFILTER_HPP__

#include <stdint.h>

#define FILTER_ALLOC(x) malloc((x))

  template <uint32_t BITS>
  class BitFilter
  {
      static const uint32_t WORD_SIZE   = 8 * sizeof(uintptr_t);
      static const uint32_t WORD_BLOCKS = BITS / WORD_SIZE;

	  uintptr_t word_filter[WORD_BLOCKS];
	  
      static uint32_t hash(const void* const key)
      {
          return (((uintptr_t)key) >> 3) % BITS;
      }

	  public:

      BitFilter() { clear(); }
	  
      void add(const void* const val) volatile
      {
          const uint32_t index  = hash(val);
          const uint32_t block  = index / WORD_SIZE;
          const uint32_t offset = index % WORD_SIZE;
          word_filter[block] |= (1u << offset);
      }

      bool lookup(const void* const val) const volatile
      {
          const uint32_t index  = hash(val);
          const uint32_t block  = index / WORD_SIZE;
          const uint32_t offset = index % WORD_SIZE;

          return word_filter[block] & (1u << offset);
      }
	  
      void unionwith(const BitFilter<BITS>& rhs)
      {
          for (uint32_t i = 0; i < WORD_BLOCKS; ++i)
              word_filter[i] |= rhs.word_filter[i];
      }

      void clear() volatile
      {
          for (uint32_t i = 0; i < WORD_BLOCKS; ++i)
              word_filter[i] = 0;
      }

      void fastcopy(const volatile BitFilter<BITS>* rhs) volatile
      {
          for (uint32_t i = 0; i < WORD_BLOCKS; ++i)
              word_filter[i] = rhs->word_filter[i];
      }

      bool intersect(const BitFilter<BITS>* rhs) const volatile
      {
          for (uint32_t i = 0; i < WORD_BLOCKS; ++i)
              if (word_filter[i] & rhs->word_filter[i])
                  return true;
          return false;
      }
  }; 
#endif
