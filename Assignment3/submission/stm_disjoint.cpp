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

#define CFENCE  __asm__ volatile ("":::"memory")
#define MFENCE  __asm__ volatile ("mfence":::"memory")

#define RS_SCALE (1.0 / (1.0 + RAND_MAX))
#define NUM_OF_ACCOUNTS 1000000
#define SIZE_OF_SET 40

struct arg_struct {
	int ids;
	int num_transfer;
};

struct write_set {
	int valid;
	unsigned int addr;
	unsigned int value;
};

struct read_set {
	int valid;
	unsigned int addr;
	unsigned int version;
};

struct lock_table {
	unsigned int version;
	pthread_mutex_t lock;
};

unsigned int accounts[NUM_OF_ACCOUNTS];
struct lock_table locks[NUM_OF_ACCOUNTS];
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

void tx_begin(struct write_set *ws, struct read_set *rs)
{
	for (int i = 0; i < SIZE_OF_SET; i++)
	{	
		ws[i].valid = 0;
		ws[i].value = 0;
		ws[i].addr = 0;
		rs[i].valid = 0;
		rs[i].addr = 0;
		rs[i].version = 0;
	}
}

int tx_commit(struct write_set *ws, struct read_set *rs)
{
	for (int i = 0; i < SIZE_OF_SET; i++)
	{
		if (ws[i].valid == 1)
		{
			/* if failed to lock */
			if (pthread_mutex_trylock(&(locks[ws[i].addr].lock)) == EBUSY)
			{
				for (int j = 0; j < i; j++)
				{
					if (ws[j].valid == 1)
						pthread_mutex_unlock(&(locks[ws[j].addr].lock));
				}
				
				/* abort */
				return 0;
			}
		}
	}
	
	for (int i = 0; i < SIZE_OF_SET; i++)
	{
		if (rs[i].valid == 1)
		{
			if (rs[i].version != locks[rs[i].addr].version)
			{			
				for (int j = 0; j < SIZE_OF_SET; j++)
				{
					/* unlock the wirte-set's lock */
					if(ws[j].valid == 1)
					{
						pthread_mutex_unlock(&(locks[ws[j].addr].lock));
					}
				}
				/* abort */
				return 0;
			}
		}
	}

	for (int i = 0; i < SIZE_OF_SET; i++)
	{
		if (ws[i].valid == 1)
		{
			/* write back */
			accounts[ws[i].addr] = ws[i].value;
			locks[ws[i].addr].version++;
			pthread_mutex_unlock(&(locks[ws[i].addr].lock));
		}
	}

	return 1;
}

unsigned int tx_read(struct write_set *ws, struct read_set *rs, unsigned int addr, int *err)
{
	unsigned int value;
	for (int i = 0; i < SIZE_OF_SET; i++)
	{
		if (ws[i].valid == 1 && ws[i].addr == addr)
			return ws[i].value;
	}

	if ( pthread_mutex_trylock(&(locks[addr].lock)) == EBUSY )
	{	
		*err = 1;
		return 0;
	}
	else
	{
		for (int j = 0; j < SIZE_OF_SET; j++)
		{
			if (rs[j].valid == 0)
			{
				rs[j].valid = 1;
				rs[j].addr = addr;
				rs[j].version = locks[addr].version;				
				value = accounts[addr];
				pthread_mutex_unlock(&locks[addr].lock);
				return value;
			}
		}
	}
	
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
	
	return;
}

void* th_run(void * args)
{
	long id = (long)args;
	unsigned int sender, receiver;
	int err = 0;
	unsigned int sender_balance;
	unsigned int receiver_balance;

	struct write_set *ws = (struct write_set *)malloc(sizeof(struct write_set) * SIZE_OF_SET);
	struct read_set *rs = (struct read_set *)malloc(sizeof(struct read_set) * SIZE_OF_SET);
	int padding = (int)id * accounts_allowed;
	//printf("id:%ld, padding:%d\n",id,padding);

	barrier(0);
	
	for (int i=0; i<num_transfer; i++) {	

stm_loop: 
		do {
			err = 0;
			tx_begin(ws, rs);

			for (int j = 0; j < 10; j++)
			{
				sender = (get_real_time() % accounts_allowed) + padding;
				sender_balance = tx_read(ws, rs, sender, &err);
				if (err == 1)
					goto stm_loop;

				if (sender_balance == 0)
					continue;
		
				do {
					receiver = (get_real_time() % accounts_allowed) + padding;
				} while (sender == receiver);

				receiver_balance = tx_read(ws, rs, receiver, &err);
				if (err == 1)
					goto stm_loop;

				tx_write(ws, sender, sender_balance - 50);
				tx_write(ws, receiver, receiver_balance + 50);
			}
		
		} while (!tx_commit(ws, rs));
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
	num_transfer = 100000 / total_threads;
	accounts_allowed = NUM_OF_ACCOUNTS / total_threads;
	// initialize accounts
	for (int i = 0; i < NUM_OF_ACCOUNTS; i++)
	{ 
		accounts[i] = 1000;
		locks[i].version = 0;
		if (pthread_mutex_init(&locks[i].lock, NULL) != 0)
    	{
     		printf("\n mutex init failed\n");
       		return 1;
    	}
	}

	pthread_attr_t thread_attr;
	pthread_attr_init(&thread_attr);

	pthread_t client_th[300];

	long ids = 1;

	for (int i = 1; i<total_threads; i++) {
		pthread_create(&client_th[ids-1], &thread_attr, th_run, (void *)ids);
		ids++;
	}
	
	// Verification
	volatile unsigned int total_balance = 0;
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
	
	printf("Time: %lld\n", get_real_time() - start);
	
	// Verification
	total_balance = 0;
	for (int j = 0; j < NUM_OF_ACCOUNTS; j++)
	{
		total_balance = total_balance + accounts[j];
	}

	printf("After: total balance = %u\n", total_balance);

	
	for (int i = 0; i < NUM_OF_ACCOUNTS; i++)
	{
		pthread_mutex_destroy(&locks[i].lock);
	}

	return 0;
}
