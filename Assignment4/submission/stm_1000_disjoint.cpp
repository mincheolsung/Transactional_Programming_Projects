#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>
#include <errno.h>
#include "rand_r_32.h"

#define CFENCE  __asm__ volatile ("":::"memory")
#define MFENCE  __asm__ volatile ("mfence":::"memory")

#define IS_LOCKED(lock) lock & 1 == 1

#define UNLOCK(lock, new_ver) lock = new_ver << 1

#define GET_VERSION(lock) lock >> 1

#define IS_UNCHANGED(old_lock_val, lock)  (old_lock_val & ~1) == lock

#define TRY_LOCK(lock) __sync_bool_compare_and_swap(&(lock), (lock) & ~1, lock | 1)

#define DISJOINT
#define NUM_OF_ACCOUNTS 1000
#define NUM_OF_TRANSFER 100000
#define SIZE_OF_SET 20

volatile unsigned int global_clock;

struct arg_struct {
	int ids;
	int num_transfer;
};

struct write_set {
	int valid;
	unsigned int  addr;
	unsigned int  value;
};

struct read_set {
	int valid;
	unsigned int  addr;
};

volatile unsigned int accounts[NUM_OF_ACCOUNTS];
volatile unsigned int locks[NUM_OF_ACCOUNTS];
int accounts_allowed;

int total_threads;
int	num_transfer;

inline unsigned long long get_real_time() {
        struct timespec time;
    
	clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    
	return time.tv_sec * 1000000000L + time.tv_nsec;
}

/**
 *  Support a few lightweight barriers
 */
void
barrier(int which)
{
    static volatile int barriers[16] = {0};
   	CFENCE;
    __sync_fetch_and_add(&barriers[which], 1);
    while (barriers[which] != total_threads) { }
    CFENCE;
}

void
signal_callback_handler(int signum)
{
   // Terminate program
   exit(signum);
}

volatile bool ExperimentInProgress = true;
static void catch_SIGALRM(int sig_num)
{
    ExperimentInProgress = false;
}

void tx_begin(struct write_set *ws, struct read_set *rs, unsigned int *rv)
{
	for (int i = 0; i < SIZE_OF_SET; i++)
	{	
		ws[i].valid = 0;
		ws[i].value = 0;
		ws[i].addr = 0;
		rs[i].valid = 0;
		rs[i].addr = 0;
	}
	*rv = global_clock; 
}

int tx_commit(struct write_set *ws, struct read_set *rs, unsigned int rv)
{
	unsigned int wv = 0;
	volatile unsigned int old_version = 0;

	for (int i = 0; i < SIZE_OF_SET; i++)
	{
		//printf("i:%d, valid:%d, addr: %llu, value: %llu\n", i, ws[i].valid, ws[i].addr, ws[i].value);
		if (ws[i].valid == 1)
		{
			if (!TRY_LOCK(locks[ws[i].addr]))
			{
				/* if fails to lock */
				for (int j = 0; j < i; j++)
				{
					if (ws[j].valid == 1)
						old_version = GET_VERSION(locks[ws[j].addr]);
						UNLOCK(locks[ws[j].addr], old_version);
				}
				/* abort */
				return 0;
			}
		}
	}

	wv = __sync_fetch_and_add(&global_clock, 1);
MFENCE;
	/* Validate the read-set */
	for (int i = 0; i < SIZE_OF_SET; i++)
	{
loop:
		if (i == SIZE_OF_SET)
			break;

		if (rs[i].valid == 1)
		{
			for (int h = 0; h < SIZE_OF_SET; h++)
			{
				if ( rs[i].addr == ws[h].addr )
				{
					i++;
					goto loop;
				}
			}

			if ( GET_VERSION(locks[rs[i].addr]) > rv || IS_LOCKED(locks[rs[i].addr]) )
			{
				for (int j = 0; j < SIZE_OF_SET; j++)
				{
					/* unlock the wirte-set's lock */
					if(ws[j].valid == 1)
					{
						old_version = GET_VERSION(locks[ws[j].addr]);
						UNLOCK(locks[ws[j].addr], old_version);
					}
				}
				/* abort */
				return 0;
			}
		}
	}
MFENCE;
	/* Now transaction is valid and can be committed */
	for (int i = 0; i < SIZE_OF_SET; i++)
	{
		if (ws[i].valid == 1)
		{
			if (IS_LOCKED(locks[ws[i].addr]))
			{	
				/* write back */
				accounts[ws[i].addr] = ws[i].value;
				UNLOCK(locks[ws[i].addr], wv);
			}
			else
			{
				printf("this never happens\n");
			}
		}
	}
MFENCE;
	return 1;
}

unsigned int tx_read(struct write_set *ws, struct read_set *rs, unsigned int addr, unsigned int rv, int *err)
{
	unsigned int value = 0;
	unsigned int v1, v2;
	for (int i = 0; i < SIZE_OF_SET; i++)
	{
		if (ws[i].valid == 1 && ws[i].addr == addr)
			return ws[i].value;
	}

	v1 = GET_VERSION(locks[addr]);
	CFENCE;
	value = accounts[addr]; 
	CFENCE;
	v2 = GET_VERSION(locks[addr]);
	
	if ( (v2 <= rv) && (v1 == v2) && !(IS_LOCKED(locks[addr])) )
	{
		for (int j = 0; j < SIZE_OF_SET; j++)
		{
			if (rs[j].valid == 0)
			{
				rs[j].valid = 1;
				rs[j].addr = addr;
				return value;
			}
		}

	}
	else
	{
		*err = 1;
		return 0;
	}
	
	printf("tx_read: You should never see this message\n");
	return -1;
}

void tx_write(struct write_set *ws, unsigned int addr, unsigned int value)
{
	for (int i = 0; i < SIZE_OF_SET; i++)
	{	
		if ( ws[i].valid == 1  && ws[i].addr == addr)
		{	
			ws[i].value = value;
			return;
		}
	}

	for (int j = 0; j < SIZE_OF_SET; j++)
	{
		if (ws[j].valid == 0)
		{	
			ws[j].valid = 1;
			ws[j].addr = addr;
			ws[j].value = value;
			return;
		}
	}
	
	printf("tx_write: You should never see this message\n");
	return;
}

void* th_run(void * args)
{
	long id = (long)args;
	unsigned int sender, receiver;
	int err = 0;
	unsigned int sender_balance;
	unsigned int receiver_balance;
	unsigned int rv = 0;
	unsigned int seed = (unsigned int)get_real_time();
	int padding = (int)id * accounts_allowed;

	struct write_set *ws = (struct write_set *)malloc(sizeof(struct write_set) * SIZE_OF_SET);
	struct read_set *rs = (struct read_set *)malloc(sizeof(struct read_set) * SIZE_OF_SET);

	barrier(0);
	
	for (int i=0; i<num_transfer; i++) {	
stm_loop: 
		do {
			err = 0;
			tx_begin(ws, rs, &rv);

			for (int j = 0; j < 10; j++)
			{
#ifndef DISJOINT
				sender = rand_r_32(&seed) % NUM_OF_ACCOUNTS;
				do {
					receiver = rand_r_32(&seed) % NUM_OF_ACCOUNTS;
				} while (sender == receiver);
#else
				sender = (rand_r_32(&seed) % accounts_allowed) + (unsigned int)padding;
				do {
					receiver = (rand_r_32(&seed) % accounts_allowed) + (unsigned int)padding;
				} while (sender == receiver);
#endif
				sender_balance = tx_read(ws, rs, sender, rv, &err);
				if (err == 1)
					goto stm_loop;
				
				if (sender_balance == 0)
					continue;

				receiver_balance = tx_read(ws, rs, receiver, rv, &err);
				if (err == 1)
					goto stm_loop;

				tx_write(ws, sender, sender_balance - 50);
				tx_write(ws, receiver, receiver_balance + 50);
			}
		
		} while (!tx_commit(ws, rs, rv));
	}

	return 0;
}

int main(int argc, char* argv[])
{
//	signal(SIGINT, signal_callback_handler);
	if (argc < 2) {
		printf("Usage test threads#\n");
		exit(0);
	}

    total_threads = atoi(argv[1]);
	num_transfer = NUM_OF_TRANSFER / total_threads;
	accounts_allowed = NUM_OF_ACCOUNTS / total_threads;

	// initialize accounts
	for (int i = 0; i < NUM_OF_ACCOUNTS; i++)
	{ 
		accounts[i] = 1000;
		locks[i] = 0;
	}

	/* Initialize global clock */
	global_clock = 0;

	pthread_attr_t thread_attr;
	pthread_attr_init(&thread_attr);

	pthread_t client_th[300];

	long ids = 1;

	for (int i = 1; i<total_threads; i++) {
		pthread_create(&client_th[ids-1], &thread_attr, th_run, (void *)ids);
		ids++;
	}
	
	// Verification
	unsigned int total_balance = 0;
	for (int j = 0; j < NUM_OF_ACCOUNTS; j++)
	{
		total_balance = total_balance + accounts[j];
	}
	printf("Before: total balance = %u\n", total_balance);
	
	unsigned long long start = get_real_time();
	th_run(0);
	
	for (int i=0; i<total_threads-1; i++) {
		pthread_join(client_th[i], NULL);
	}
	
	printf("Time %llu\n", get_real_time() - start);
	
	// Verification
	total_balance = 0;
	for (int j = 0; j < NUM_OF_ACCOUNTS; j++)
	{
	//	printf("%llu\n", accounts[j]);
		total_balance = total_balance + accounts[j];
	}

	printf("After: total balance = %u\n", total_balance);
	
	return 0;
}
