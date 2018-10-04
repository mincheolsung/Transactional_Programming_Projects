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

struct account {
	unsigned balance;
	pthread_mutex_t lock;
};
struct account accounts[1000000];

struct arg_struct {
	int ids;
	int num_transfer;
};

int total_threads;

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

void* th_run(void * args)
{
	struct arg_struct this_args = *(struct arg_struct *)args;
	int id = this_args.ids;
	int num_transfer = this_args.num_transfer;
	unsigned long long sender = 0, receiver = 0;
	
	barrier(0);

	for (int i=0; i<num_transfer; i++) {
		sender = get_real_time()%1000000;
		
		pthread_mutex_lock(&accounts[sender].lock);
		if (accounts[sender].balance == 0)
		{	
			pthread_mutex_unlock(&accounts[sender].lock);
			continue;
		}

		accounts[sender].balance = accounts[sender].balance - 50;
		pthread_mutex_unlock(&accounts[sender].lock);

		do {
			receiver  = get_real_time()%1000000;
		} while (sender == receiver);	
		
		pthread_mutex_lock(&accounts[receiver].lock);
		accounts[receiver].balance = accounts[receiver].balance + 50;
		pthread_mutex_unlock(&accounts[receiver].lock);
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

	// initialize accounts
	for (int i = 0; i < 1000000; i++)
	{ 
		accounts[i].balance = 1000;
		if (pthread_mutex_init(&(accounts[i].lock), NULL) != 0)
    	{
       		printf("\n mutex init failed\n");
       		return 1;
    	}
	}

	pthread_attr_t thread_attr;
	pthread_attr_init(&thread_attr);

	pthread_t client_th[300];

	struct arg_struct args[total_threads];
	for (int h = 0; h < total_threads; h++)
	{
		args[h].ids = h;
		args[h].num_transfer = 100000 / total_threads;
	}

	for (int i = 1; i<total_threads; i++) {
		pthread_create(&client_th[args[i].ids-1], &thread_attr, th_run, (void *)&args[i]);
	}
	
	unsigned long long start = get_real_time();
	th_run(&args[0]);

	for (int i=0; i<total_threads-1; i++) {
		pthread_join(client_th[i], NULL);
	}
	
	printf("%lld\n", get_real_time() - start);
	
	// Verification
	volatile unsigned total_balance = 0;
	for (int i = 0; i < 1000000; i++)
	{
		total_balance += accounts[i].balance;
	}

	if (total_balance != 1000000000)
		printf("Total balance = %u\n", total_balance);

	for (int i = 0; i < 1000000; i++)
	{ 
		pthread_mutex_destroy(&(accounts[i].lock));
	}

	return 0;
}
