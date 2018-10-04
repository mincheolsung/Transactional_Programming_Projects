#ifndef TM_HPP
#define TM_HPP 1

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

#define TABLE_SIZE 1048576

#define FORCE_INLINE __attribute__((always_inline)) inline
#define CACHELINE_BYTES 64
#define CFENCE              __asm__ volatile ("":::"memory")
#define MFENCE  __asm__ volatile ("mfence":::"memory")

using stm::WriteSetEntry;
using stm::WriteSet;

struct pad_word_t
  {
      volatile uintptr_t val;
      char pad[CACHELINE_BYTES-sizeof(uintptr_t)];
  };

struct lock_entry {
	volatile uint64_t lock_owner;
	volatile uint64_t version;
};


extern lock_entry* lock_table;

#define nop()       __asm__ volatile("nop")
#define pause()		__asm__ ( "pause;" )

#define NUM_STRIPES  1048576



inline unsigned long long get_real_time() {
	struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);

    return time.tv_sec * 1000000000L + time.tv_nsec;
}

#define ACCESS_SIZE 102400

struct Tx_Context {
	int id;
	jmp_buf scope;
	uintptr_t start_time;
	int reads_pos;
	uint64_t reads[ACCESS_SIZE];
	int writes_pos;
	int granted_writes_pos;
	uint64_t writes[ACCESS_SIZE];
	bool granted_writes[ACCESS_SIZE];
	WriteSet* writeset;
	long commits =0, aborts =0;
};

extern __thread Tx_Context* Self;

extern pad_word_t global_clock;

#define TM_TX_VAR	Tx_Context* tx = (Tx_Context*)Self;

#define TM_ARG , Tx_Context* tx
#define TM_ARG_ALONE Tx_Context* tx
#define TM_PARAM ,tx


#define TM_FREE(a)  /*nothing for now*/

#define TM_ALLOC(a) malloc(a)

FORCE_INLINE void tm_abort(Tx_Context* tx, int explicitly);

FORCE_INLINE uint64_t tm_read(uint64_t* addr, Tx_Context* tx)
{
    WriteSetEntry log((void**)addr);
    bool found = tx->writeset->find(log);
    if (__builtin_expect(found, false))
		return log.val;


	uint64_t index = (reinterpret_cast<uint64_t>(addr)>>3) % TABLE_SIZE;
	lock_entry* entry_p = &(lock_table[index]);

	uint64_t v1 = entry_p->version;
	CFENCE;
	uint64_t val = *addr;
	CFENCE;
	uint64_t v2 = entry_p->version;
	if (v1 > tx->start_time || (v1 != v2) || entry_p->lock_owner) {
		tm_abort(tx, 0);
	}
	int r_pos = tx->reads_pos++;
	tx->reads[r_pos] = index;
	return val;
}

FORCE_INLINE void tm_write(uint64_t* addr, uint64_t val, Tx_Context* tx)
{
    bool alreadyExists = tx->writeset->insert(WriteSetEntry((void**)addr, *((uint64_t*)(&val))));
    if (!alreadyExists) {
		int w_pos = tx->writes_pos++;
		tx->writes[w_pos] = (reinterpret_cast<uint64_t>(addr)>>3) % TABLE_SIZE;
		tx->granted_writes[w_pos] = false;
    }
}

#define TM_READ(var)       tm_read(&var, tx)
#define TM_WRITE(var, val) tm_write(&var, val, tx)


FORCE_INLINE void spin64() {
	for (int i = 0; i < 64; i++)
		nop();
}

FORCE_INLINE void thread_init(int id) {
	if (!Self) {
		Self = new Tx_Context();
		Tx_Context* tx = (Tx_Context*)Self;
		tx->id = id;
		tx->writeset = new WriteSet(ACCESS_SIZE);
	}
}

FORCE_INLINE void tm_sys_init() {
	lock_table = (lock_entry*) malloc(sizeof(lock_entry) * TABLE_SIZE);

	for (int i=0; i < TABLE_SIZE; i++) {
		lock_table[i].lock_owner = 0;
		lock_table[i].version = 0;
	}
}


FORCE_INLINE void tm_abort(Tx_Context* tx, int explicitly)
{
	tx->aborts++;
	//restart the tx
    longjmp(tx->scope, 1);
}

FORCE_INLINE void tm_commit(Tx_Context* tx)
{
	if (tx->writeset->size() == 0) { //read-only
		return;
	}

	bool failed = false;
	for (int i = 0; i < tx->writes_pos; i++) {
		lock_entry* entry_p = &(lock_table[tx->writes[i]]);
		if (entry_p->lock_owner == (tx->id + 1)) continue;
		if (!__sync_bool_compare_and_swap(&(entry_p->lock_owner), 0, tx->id+1)) {
			failed = true;
			break;
		}
		tx->granted_writes[i] = true;
	}
	if (failed) { //unlock and abort
		for (int i = 0; i < tx->writes_pos; i++) {
			if (tx->granted_writes[i]) {
				lock_entry* entry_p = &(lock_table[tx->writes[i]]);
				entry_p->lock_owner = 0;
			} else {
				break;
			}
		}

		tm_abort(tx, 0);
	}

 	bool do_abort = false;
	//validate reads
	for (int i = 0; i < tx->reads_pos; i++) {
		lock_entry* entry_p = &(lock_table[tx->reads[i]]);
		if (entry_p->version > tx->start_time || (entry_p->lock_owner > 0 && entry_p->lock_owner != tx->id + 1)) {
			do_abort = true;
			break;
		}
	}

	if (do_abort) {
		for (int i = 0; i < tx->writes_pos; i++) {
			lock_entry* entry_p = &(lock_table[tx->writes[i]]);
			entry_p->lock_owner = 0;
		}
		tm_abort(tx, 0);
	}

	tx->writeset->writeback();
	MFENCE;

	uintptr_t next_ts = __sync_fetch_and_add(&(global_clock.val), 1) + 1;

	//update versions & unlock
	for (int i = 0; i < tx->writes_pos; i++) {
		lock_entry* entry_p = &(lock_table[tx->writes[i]]);
		entry_p->version = next_ts;
		entry_p->lock_owner = 0;
	}
	tx->commits++;
}

#define TM_BEGIN												\
	{															\
		Tx_Context* tx = (Tx_Context*)Self;          			\
		uint32_t abort_flags = _setjmp (tx->scope);				\
		{														\
			tx->reads_pos =0;									\
			tx->writes_pos =0;									\
			tx->granted_writes_pos =0;							\
			tx->writeset->reset();								\
			tx->start_time = global_clock.val;


#define TM_END                                  	\
			tm_commit(tx);                          \
		}											\
	}

#endif //TM_HPP
