#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <errno.h>


#define CFENCE   __asm__ volatile ("":::"memory")

/**
 *  Support a few lightweight barriers
 */
/*void
barrier(uint32_t which)
{
    static volatile uint32_t barriers[16] = {0};
    CFENCE;
    __sync_fetch_and_add(&barriers[which], 1);
    while (barriers[which] != total_threads) { }
    CFENCE;
}*/

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

volatile int counter = 0;
pthread_mutex_t mutex;

void* th_run(void * args)
{
		long id = (long)args;
        int localCounter = 0;
        for (int i=0; i<1000000; i++) {
			pthread_mutex_lock(&mutex);
            counter++;
			pthread_mutex_unlock(&mutex);
            localCounter++;
        }
        printf("Thread %ld local counter = %d and global counter = %d\n", id, localCounter, counter);
        return 0;
}

int main(int argc, char* argv[])
{
        //signal(SIGINT, signal_callback_handler);

        if (argc < 2) {
                printf("Usage test threads#\n");
                exit(0);
        }

        int total_threads = atoi(argv[1]);

        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);

		if (pthread_mutex_init(&mutex, NULL) != 0)
        {
			printf("\n mutex init failed\n");
			return 0;
		}

        pthread_t client_th[300];
        long ids = 1;
        for (int i = 1; i<total_threads; i++) {
                pthread_create(&client_th[ids-1], &thread_attr, th_run, (void*)ids);
                ids++;
        }
        th_run(0);

        for (int i=0; i<ids-1; i++) {
                pthread_join(client_th[i], NULL);
        }
		pthread_mutex_destroy(&mutex);

        printf("Counter = %d\n", counter);

        return 0;
}

//Build with 
//g++ test_threads.cpp -o test -lpthread

//Sample output
/*
./test

Thread 1 local counter = 1000000 and global counter = 548790
Thread 0 local counter = 1000000 and global counter = 1036543
Thread 3 local counter = 1000000 and global counter = 1070894
Thread 2 local counter = 1000000 and global counter = 1639155
Counter = 1639155

*/
