// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"
#include "lock.h"
#include "zdebug.h"

lock_server_cache::lock_server_cache()
{
    pthread_mutex_init(&server_mutex, NULL);
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
    map<lock_protocol::lockid_t, CacheLock>::iterator it;
    int ret = lock_protocol::OK;
    bool flag = false;
    string holder_id;
    {
        ScopedLock _(&server_mutex);
        it = locks.find(lid);
        if (it == locks.end()) {
            CacheLock &c = locks[lid];
            c.client = id;
            c.state = CacheLock::LOCKED;
            pthread_cond_init(&c.cond, NULL);
        } else {
            CacheLock::State st = it->state;
            switch(it->state) {
                case CacheLock::FREE:
                    it->state = CacheLock::LOCKED;
                    it->client = id;
                    break;
                case CacheLock::LOCKED:
                    it->state = CacheLock::LOCKED_AND_WAIT;
                    holder_id = it->client;
                    flag = true;
                    break;
                case CacheLock::LOCKED_AND_WAIT:
                case CacheLock::ORDERED:
                default:
                    ret = lock_protocol::RETRY;
                    break;
            }
        }
    }
    if (flag) {
        handle holder(holder_id);
        rpcc * cl = holder.safebind();
        if (cl) {
            int r;
            int rs = cl->call(rlock_protocol::revoke, lid, r);
            if (rs != rlock_protocol::OK) {
                ERR("call revoke failed");
            }
        } else {
            ERR("cannot safe bind");
        }
    }
    return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
    ScopedLock s(&server_mutex);
    return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

