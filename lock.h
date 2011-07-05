#ifndef __LOCK_H__
#define __LOCK_H__

#include <pthread.h>
#include <string>
#include "handle.h"

struct Lock {
    pthread_cond_t cond;
    int client;
};

struct CacheLock {
    enum State {
        FREE,
        LOCKED,
        LOCKED_AND_WAIT,
        ORDERED
    };
    pthread_cond_t cond;
    std::string client;
    CacheLockState state;

};

#endif //__LOCK_H__
