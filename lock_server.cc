// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "rpc/slock.h"

lock_server::lock_server(): nacquire (0), server_mutex (PTHREAD_MUTEX_INITIALIZER) {
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r) {
    lock_protocol::status ret = lock_protocol::OK;
    printf("stat request from clt %d\n", clt);
    r = nacquire;
    return ret;
}

//#define DEBUG

#ifdef DEBUG
    #define Z(args...) printf(args)
#else
    #define Z(args...)
#endif

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r) {
    ScopedLock s(&server_mutex);
    map<lock_protocol::lockid_t, Lock>::iterator it;
    it = locks.find(lid);
    if (it == locks.end()) {
        Lock l;
        l.client = clt;
        l.cond = PTHREAD_COND_INITIALIZER;
        locks[lid] = l;
        Z("client %d got empty lock %d\n", locks[lid].client, lid);
    } else {
        Lock * l = &it->second;
        Z("client %d waiting for busy lock %d\n", clt, lid);
        while (l->client > 0) {        // held by others
            pthread_cond_wait(&l->cond, &server_mutex);
        }
        Z("client %d got busy lock %d\n", clt, lid);
        locks[lid].client = clt;
    }
    return lock_protocol::OK;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r) {
    ScopedLock s(&server_mutex);
    map<lock_protocol::lockid_t, Lock>::iterator it;
    it = locks.find(lid);
    if (it != locks.end()) {
        Z("client %d released lock %d\n", clt, lid);
        it->second.client = -1;
        int ret = pthread_cond_broadcast(&((*it).second.cond));
        if (ret)
            return lock_protocol::RETRY;
    }
    return lock_protocol::OK;
}

