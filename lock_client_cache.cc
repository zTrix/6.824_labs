// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

using namespace std;

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);
  pthread_mutex_init(&mutex, NULL);
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
    ScopedLock _(&mutex);
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    if (!locks.count(lid)) {        // if not exist
       ClientCacheLock &ccl = locks[lid];
       ccl.state = ClientCacheLock::NONE;
       pthread_init_cond(&ccl.wait_cond, NULL);
       pthread_init_cond(&ccl.retry_cond, NULL);
       ccl.owner = -1;
    };
    it = locks.find(lid);
    switch(it->state) {
        case ClientCacheLock::NONE:
            it->state = ClientCacheLock::ACQUIRING;
            int ret = lock_protocol::RETRY;
            int r;
            while (true) {
                ret = require(lid, id, r);
                if (ret == lock_protocol::OK) {
                    break;
                }
                pthread_cond_wait(&it->retry_cond, &mutex);
            }
            break;

        case ClientCacheLock::FREE:
            break;

        case ClientCacheLock::LOCKED:
        case ClientCacheLock::ACQUIRING:
            while (it->state != ClientCacheLock::FREE) {
                pthread_cond_wait(&it->wait_cond, &mutex);
                if (it->state != ClientCacheLock::ACQUIRING) {
                    break;
                }
            }
            if (it->state == ClientCacheLock::NONE) {
                it->state = ClientCacheLock::ACQUIRING;
                int ret = lock_protocol::RETRY;
                int r;
                while (true) {
                    ret = require(lid, id, r);
                    if (ret == lock_protocol::OK) {
                        break;
                    }
                    pthread_cond_wait(&it->retry_cond, &mutex);
                }
            }
            break;

        case ClientCacheLock::RELEASING:
            break;

        default:
            break;
    }
    it->state = ClientCacheLock::LOCKED;
    it->owner = pthread_self();
    return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
    ScopedLock _(&mutex);
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    it = locks.find(lid);
    if (it == locks.end()) {
        ERR("hey, what are you releasing");
    } else if (it->state == ClientCacheLock::NONE) {

    }
    int r;
    int ret = cl->call(lock_protocol::release, lid, id, r);
    if (ret != lock_protocol::OK) {
        ERR("release failed");
    };
    pthread_signal();
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
    ScopedLock _(&mutex);
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    it = locks.find(lid);
    if (it == locks.end()) {
        ERR("lock not found");
        return lock_protocol::RETRY;
    }
    switch(it->state) {
        case ClientCacheLock::NONE:
            ERR("hey, I do not have the lock");
            break;

        case ClientCacheLock::FREE:
            it->state = ClientCacheLock::NONE;
            break;

        case ClientCacheLock::LOCKED:
            break;
    }
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
    ScopedLock _(&mutex);
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    it = locks.find(lid);
    if (it == locks.end()) {
        ERR("lock not found");
        return lock_protocol::RETRY;
    }
    pthread_cond_signal(&it->retry_cond);
    return lock_protocol::OK;
}



