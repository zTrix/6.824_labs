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

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r) {
    ScopedLock s(&server_mutex);
    map<lock_protocol::lockid_t, Lock>::iterator it;
    it = locks.find(lid);
    if (it == locks.end()) {
        printf("client %d locks not found\n", clt);
        Lock l;
        l.client = clt;
        l.cond = PTHREAD_COND_INITIALIZER;
        locks[lid] = l;
    } else {
        Lock * l = &it->second;
        printf("client %d \n", clt);
        while (l->client > 0) {        // held by others
            pthread_cond_wait(&l->cond, &server_mutex);
        }
        locks[lid].client = clt;
        l->cond = PTHREAD_COND_INITIALIZER;
    }
    return lock_protocol::OK;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r) {
    ScopedLock s(&server_mutex);
    map<lock_protocol::lockid_t, Lock>::iterator it;
    it = locks.find(lid);
    if (it == locks.end()) {

    } else {
        it->second.client = -1;
        pthread_cond_signal(&((*it).second.cond));
    }
    return lock_protocol::OK;
}

