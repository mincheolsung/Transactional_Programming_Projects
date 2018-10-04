#include "tm_thread.hpp"
#include <pthread.h>
#include <signal.h>
#include <pthread.h>

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

__thread Tx_Context* Self;

pad_word_t global_clock = {0};

lock_entry* lock_table;

long int    FALSE = 0,
    TRUE  = 1;
