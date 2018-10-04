#include "ring_stm.hpp"
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


struct ring_entry *ring;
uint64_t ring_index = 0;

long int    FALSE = 0,
    TRUE  = 1;
