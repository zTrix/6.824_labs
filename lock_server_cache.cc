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
        if (!locks.count(lid)) {
            CacheLock &c = locks[lid];
            c.client = "";
            c.state = CacheLock::FREE;
        }
        it = locks.find(lid);
        switch(it->second.state) {
            case CacheLock::FREE:
                it->second.state = CacheLock::LOCKED;
                it->second.client = id;
                ret = lock_protocol::OK;
                if (it->second.queue.size()) {
                    ERR("how can free lock has a queue");
                }
                Z("%s got lock %llx", id.c_str(), lid);
                break;

            case CacheLock::LOCKED:
                it->second.state = CacheLock::LOCKED_AND_WAIT;
                it->second.queue.insert(id);
                holder_id = it->second.client;
                need_revoke = true;
                ret = lock_protocol::RETRY;
                Z("%s, RETRY from lock %llx, which is LOCKED", id.c_str(), lid);
                break;

            case CacheLock::LOCKED_AND_WAIT:
                // add to wait queue
                it->second.queue.insert(id);
                ret = lock_protocol::RETRY;
                Z("%s, RETRY from lock %llx, which is LOCKED_AND_WAIT", id.c_str(), lid);
                break;

            case CacheLock::ORDERED:
                if (it->second.queue.count(id)) {
                    // he comes to get what he deserves
                    it->second.client = id;
                    it->second.queue.erase(id);
                    if (it->second.queue.size()) {
                        it->second.state = CacheLock::LOCKED_AND_WAIT;
                        need_revoke = true;
                        holder_id = id;
                    } else {
                        it->second.state = CacheLock::LOCKED;
                    }
                    ret = lock_protocol::OK;
                    Z("%s got ORDERED lock %llx, need_revoke = %d", id.c_str(), lid, need_revoke);
                } else {
                    it->second.queue.insert(id);
                    ret = lock_protocol::RETRY;
                    Z("%s, RETRY from lock %llx, which is ORDERED", id.c_str(), lid);
                }
                break;

            default:
                ret = lock_protocol::RETRY;
                break;
        }
    }
    if (need_revoke) {
        handle holder(holder_id);
        rpcc * cl = holder.safebind();
        if (cl) {
            int r;
            Z("%s, revoking lock %llx from %s", id.c_str(), lid, holder_id.c_str());
            int rs = cl->call(rlock_protocol::revoke, lid, r);
            if (rs != rlock_protocol::OK) {
                ERR("call revoke failed");
                // TODO : should change the lock state back
            }
        } else {
            ERR("cannot safe bind");
        }
    }
    //Z("%s, return %d from acquire for lock %llx", id.c_str(), ret, lid);
    return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
    bool inform = false;
    string addr;
    map<lock_protocol::lockid_t, CacheLock>::iterator it;
    {
        ScopedLock _(&server_mutex);
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
                    Z("%s released lock %llx", id.c_str(), lid);
                    break;

                case CacheLock::LOCKED_AND_WAIT:
                    it->second.state = CacheLock::ORDERED;
                    it->second.client = "";
                    inform = true;
                    addr = *(it->second.queue.begin());
                    Z("%s released lock %llx and ready to call retry", id.c_str(), lid);
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
    }       // scoped lock end
    if (inform) {
        handle h(addr);
        rpcc * cl = h.safebind();
        if (cl) {
            int r;
            Z("%s, calling retry RPC to %s, lid is %llx", id.c_str(), addr.c_str(), lid);
            int rs = cl->call(rlock_protocol::retry, lid, r);
            if (rs != rlock_protocol::OK) {
                ERR("retry failed");
            } else {
                Z("%s, retry RPC returned OK from %s, lid is %llx", id.c_str(), addr.c_str(), lid);
            }
        } else {
            ERR("safe bind failed");
        }
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

