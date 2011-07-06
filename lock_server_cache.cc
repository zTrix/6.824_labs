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
    int ret = lock_protocol::RETRY;
    bool need_revoke = false;
    string holder_id;
    {
        ScopedLock _(&server_mutex);
        it = locks.find(lid);
        if (it == locks.end()) {
            CacheLock &c = locks[lid];
            c.client = id;
            c.state = CacheLock::LOCKED;
            ret = lock_protocol::OK;
        } else {
            switch(it->second.state) {
                case CacheLock::FREE:
                    it->second.state = CacheLock::LOCKED;
                    it->second.client = id;
                    ret = lock_protocol::OK;
                    if (it->second.queue.size()) {
                        ERR("how can free lock has a queue");
                    }
                    break;
                case CacheLock::LOCKED:
                    it->second.state = CacheLock::LOCKED_AND_WAIT;
                    it->second.queue.push_back(id);
                    holder_id = it->second.client;
                    need_revoke = true;
                    ret = lock_protocol::RETRY;
                    break;
                case CacheLock::LOCKED_AND_WAIT:
                    // add to wait queue
                    it->second.queue.push_back(id);
                    ret = lock_protocol::RETRY;
                    break;
                case CacheLock::ORDERED:
                    if (!id.compare(it->second.queue.front())) {
                        // he comes to get what he deserves
                        it->second.client = id;
                        it->second.queue.pop_front();
                        if (it->second.queue.size()) {
                            it->second.state = CacheLock::LOCKED_AND_WAIT;
                        } else {
                            it->second.state = CacheLock::LOCKED;
                        }
                    } else {
                        it->second.queue.push_back(id);
                        ret = lock_protocol::RETRY;
                    }
                default:
                    ret = lock_protocol::RETRY;
                    break;
            }
        }
    }
    if (need_revoke) {
        handle holder(holder_id);
        rpcc * cl = holder.safebind();
        if (cl) {
            int r;
            int rs = cl->call(rlock_protocol::revoke, lid, r);
            if (rs != rlock_protocol::OK) {
                ERR("call revoke failed");
                // TODO : should change the lock state back
            }
            ret = lock_protocol::RETRY;
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
    ScopedLock _(&server_mutex);
    map<lock_protocol::lockid_t, CacheLock>::iterator it;
    it = locks.find(lid);
    if (it != locks.end()) {
        switch(it->second.state) {
            case CacheLock::FREE:
                ERR("who are releasing a free lock????, %s", id.c_str());
                break;

            case CacheLock::LOCKED:
                it->second.state = CacheLock::FREE;
                it->second.client = "";
                if (it->second.queue.size()) {
                    ERR("nani ? queue size not zero");
                }
                break;

            case CacheLock::LOCKED_AND_WAIT:
                {
                    it->second.state = CacheLock::ORDERED;
                    it->second.client = "";
                    string addr = it->second.queue.front();
                    handle h(addr);
                    rpcc * cl = h.safebind();
                    if (cl) {
                        int r;
                        int rs = cl->call(rlock_protocol::retry, lid, r);
                        if (rs != rlock_protocol::OK) {
                            ERR("retry failed");
                        }
                    } else {
                        ERR("safe bind failed");
                    }
                }
                break;

            case CacheLock::ORDERED:
                ERR("who are releasing an ordered lock?? %s", id.c_str());
                break;

            default:
                break;
        }
    } else {
        ERR("releasing an non-existing lock");
    }
    return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

