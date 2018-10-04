#ifndef RING_TM_HPP
#define RING_TM_HPP 1

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "rand_r_32.h"
#include "WriteSet.hpp"
#include "BitFilter.h"

#define FILTER_SIZE 4096
#define ACCESS_SIZE 102400
#define RING_SIZE 1048576

#define COMPLETE 0
#define WRITING 1

#define FORCE_INLINE __attribute__((always_inline)) inline
#define CACHELINE_BYTES 64
#define CFENCE __asm__ volatile ("":::"memory")
#define MFENCE __asm__ volatile ("mfence":::"memory")

#define nop()       __asm__ volatile("nop")

using stm::WriteSetEntry;
using stm::WriteSet;

typedef struct ring_entry
{
	uint64_t time_stamp; 					/* commit timestamp */
	BitFilter<FILTER_SIZE> write_filter;		/* write filter */
	int status;							/* writing or complete */
} ring_entry_t;

struct Tx_Context
{
	int id;
	jmp_buf scope;
	WriteSet *write_set;		 			/* speculative writes */
	BitFilter<FILTER_SIZE> write_filter;		/* addresses to write */
	BitFilter<FILTER_SIZE> read_filter; 		/* addresses to read */
	uint64_t start;					/* logical start time */
	long commits =0, aborts =0;
};

extern __thread Tx_Context* Self;

extern struct ring_entry *ring;		/* the global ring */
extern uint64_t ring_index;		/* newest ring entry */


#define TM_TX_VAR Tx_Context* tx = (Tx_Context*)Self;

inline unsigned long long get_real_time()
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC_RAW, &time);

	return time.tv_sec * 1000000000L + time.tv_nsec;
}

FORCE_INLINE void spin64() {
	for (int i = 0; i < 64; i++)
		nop();
}

FORCE_INLINE void tm_sys_init() {
	ring = (struct ring_entry*) malloc(sizeof(struct ring_entry) * RING_SIZE);
	for (int i=0; i < RING_SIZE; i++) {
		ring[i].time_stamp = 0;
		ring[i].write_filter.clear();
		ring[i].status = COMPLETE;	
	}
}

FORCE_INLINE void ring_tm_abort(Tx_Context *tx, int explicitly)
{
	tx->aborts++;
	longjmp(tx->scope, 1);
}

FORCE_INLINE void ring_tm_validate(Tx_Context *tx)
{
	if (ring_index == tx->start)
		return;

	uint64_t suffix_end = ring_index;

	while (ring[suffix_end].time_stamp < suffix_end)
		spin64();

	for (uint64_t i = ring_index; i >= (unsigned long)tx->start + 1; i--)
	{
		if (ring[i].write_filter.intersect(&tx->read_filter))
			ring_tm_abort(tx, 0);

		if (ring[i].status == WRITING)
			suffix_end = i-1;
	}

	tx->start = suffix_end;
}

FORCE_INLINE void ring_tm_write(uint64_t *addr, uint64_t val, Tx_Context *tx)
{
	/* Add (or update) the addr and value to the write-set
	   Add the addr to the write-set signature */
	tx->write_set->insert(WriteSetEntry((void**)addr, *((uint64_t*)(&val))));
	tx->write_filter.add(addr);
}

FORCE_INLINE uint64_t ring_tm_read(uint64_t *addr, Tx_Context *tx)
{
	uint64_t val;

	WriteSetEntry log((void **)addr);
	if ( tx->write_filter.lookup(addr) && tx->write_set->find(log))
		return log.val;

	val = *addr;
	
	tx->read_filter.add(addr);

	CFENCE;

	ring_tm_validate(tx);

	return val;
}

#define TM_READ(var)	ring_tm_read(&var, tx)
#define TM_WRITE(var, val) ring_tm_write(&var, val, tx)

FORCE_INLINE void ring_tm_commit(Tx_Context *tx)
{
	if (tx->write_set->size() == 0)
		return;
again:
	uint64_t commit_time = ring_index;

	ring_tm_validate(tx);

	if (!__sync_bool_compare_and_swap(&ring_index,commit_time, commit_time + 1))
		goto again;

	ring[commit_time + 1].status = WRITING;
	ring[commit_time + 1].write_filter = tx->write_filter;
	ring[commit_time + 1].time_stamp = commit_time + 1;

	/* write back */
	tx->write_set->writeback();

	ring[commit_time + 1].status = COMPLETE;

	tx->commits++;
}

FORCE_INLINE void thread_init(int id)
{
	if (!Self) 
	{
		Self = new Tx_Context();
		Tx_Context *tx = (Tx_Context *)Self;
		tx->id = id;
		tx->write_set = new WriteSet(ACCESS_SIZE);
	}
}

#define TM_BEGIN												\
	{															\
		Tx_Context *tx = (Tx_Context *)Self;					\
		uint32_t abort_flags = _setjmp(tx->scope);				\
		{														\
			tx->write_set->reset();								\
			tx->write_filter.clear();							\
			tx->read_filter.clear();							\
			tx->start = ring_index;								\
																\
			while (ring[tx->start].status != COMPLETE ||		\
					ring[tx->start].time_stamp < tx->start ) 	\
				tx->start--;

#define TM_END							\
			ring_tm_commit(tx);			\
		}								\
	}

#endif //RING_TM_HPP
