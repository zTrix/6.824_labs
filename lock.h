#ifndef __LOCK_H__
#define __LOCK_H__

#include <pthread.h>

struct Lock {
    pthread_cond_t cond;
    int client;
};

#endif //__LOCK_H__
