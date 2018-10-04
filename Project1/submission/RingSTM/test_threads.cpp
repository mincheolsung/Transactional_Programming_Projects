#include "tm/ring_stm.hpp"
#include <pthread.h>
#include <signal.h>
#include <pthread.h>

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "tm/rand_r_32.h"

#include <errno.h>

uint64_t* accountsAll;
#define ACCOUT_NUM 1048576

unsigned int total_threads;
/**
 *  Support a few lightweight barriers
 */
void
barrier(uint32_t which)
{
    static volatile uint32_t barriers[16] = {0};
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

unsigned long long throughputs[300];

void* th_run(void * args)
{
	int id = ((long)args);
    	
    uint64_t* accounts = accountsAll;

    thread_init(id);

    barrier(0);
	unsigned int seed = id;

	if (id == 0) {
		signal(SIGALRM, catch_SIGALRM);
		alarm(1);
	}

	unsigned long long time = get_real_time();
	int tx_count = 0;
	while(ExperimentInProgress) {
		int acc1[1000];

		int acc2[1000];
		for (int j=0; j< 10; j++) {
//				acc1[j] = (ACCOUT_NUM/total_threads)*id + rand_r_32(&seed) % (ACCOUT_NUM/total_threads);
//				acc2[j] = (ACCOUT_NUM/total_threads)*id + rand_r_32(&seed) % (ACCOUT_NUM/total_threads);
				acc1[j] = rand_r_32(&seed) % ACCOUT_NUM;
				acc2[j] = rand_r_32(&seed) % ACCOUT_NUM;
		}

		tx_count++;
		TM_BEGIN
			for (int j=0; j< 10; j++) {
				TM_WRITE(accounts[acc1[j]], (TM_READ(accounts[acc1[j]]) + 50));
				TM_WRITE(accounts[acc2[j]], (TM_READ(accounts[acc2[j]]) - 50));
			}
		TM_END
	}
	time = get_real_time() - time;
    throughputs[id] = (1000000000LL * tx_count) / (time);
    TM_TX_VAR
	printf("%d: commits = %ld, aborts = %ld\n", id, tx->commits, tx->aborts);
	return 0;
}

int main(int argc, char* argv[])
{
	signal(SIGINT, signal_callback_handler);

	tm_sys_init();

	if (argc < 2) {
		printf("Usage test threads#\n");
		exit(0);
	}

    int th_per_zone = atoi(argv[1]);
	total_threads = th_per_zone? th_per_zone : 1;

	accountsAll = (uint64_t*) malloc(sizeof(uint64_t) * ACCOUT_NUM);

	long initSum = 0;
	for (int i=0; i<ACCOUT_NUM; i++) {
		accountsAll[i] = 100;
	}
	for (int i=0; i<ACCOUT_NUM; i++) {
		initSum += accountsAll[i];
	}
	printf("init sum = %ld\n", initSum);

	pthread_attr_t thread_attr;
	pthread_attr_init(&thread_attr);

	pthread_t client_th[300];
	int ids = 1;
	for (unsigned long i = 1; i < (unsigned long)th_per_zone; i++) {
		pthread_create(&client_th[ids-1], &thread_attr, th_run, (void*)i);
		ids++;
	}

	th_run(0);

	for (int i=0; i<ids-1; i++) {
		pthread_join(client_th[i], NULL);
	}

	unsigned long long totalThroughput = 0;
	for (unsigned int i=0; i<total_threads; i++) {
		totalThroughput += throughputs[i];
	}

	printf("\nThroughput = %llu\n", totalThroughput);

	long sum = 0;
	int c=0;
	for (int i=0; i<ACCOUT_NUM; i++) {
		sum += accountsAll[i];
		if (accountsAll[i] != 100) {
			c++;
		}
	}

	printf("\nsum = %ld, matched = %d, changed %d\n", sum, sum == initSum, c);

	return 0;
}
