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
    std::string client;
    State state;
    std::list<std::string> queue;
};

#endif //__LOCK_H__
