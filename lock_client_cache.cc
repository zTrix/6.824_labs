// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"
#include "zdebug.h"

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

/*
            revoke
  LOCKED -----------> RELEASING
  FREE   -----------> RELEASING and call release ------------> NONE
  NONE   -----------> revoke++
  RELEASING --------> revoke++
  ACQUIRING --------> revoke++
            
          release
  NONE -------------> ERROR
  FREE -------------> ERROR
  LOCKED -----------> FREE and signal wait_cond
  RELEASING --------> NONE and call release
  ACQUIRING --------> ERROR
        
          acquire
  NONE -------------> ACQUIRING ------------> LOCKED
  FREE -------------> LOCKED
  LOCKED -----------> pthread_wait_cond(&wait_cond) ------------> LOCKED
  RELEASING --------> wait release_cond and NONE state ---------> loop
  ACQUIRING --------> pthread_wait_cond(&wait_cond) ------------> LOCKED
*/

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
    ScopedLock _(&mutex);
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    bool loop = false;
    if (!locks.count(lid)) {        // if not exist
       ClientCacheLock &ccl = locks[lid];
       ccl.state = ClientCacheLock::NONE;
       pthread_cond_init(&ccl.wait_cond, NULL);
       pthread_cond_init(&ccl.retry_cond, NULL);
       ccl.owner = -1;
       ccl.revoke = 0;
    };
    it = locks.find(lid);
    do {
        switch(it->second.state) {
            case ClientCacheLock::NONE:
                it->second.state = ClientCacheLock::ACQUIRING;
                int ret, r;
                while (true) {
                    ret = cl->call(lock_protocol::acquire, lid, id, r);
                    if (ret == lock_protocol::OK) {
                        break;
                    }
                    pthread_cond_wait(&it->second.retry_cond, &mutex);
                }
                loop = false;
                break;

            case ClientCacheLock::FREE:
                loop = false;
                break;

            case ClientCacheLock::ACQUIRING:
            case ClientCacheLock::LOCKED:
                while (it->second.state != ClientCacheLock::FREE) {
                    pthread_cond_wait(&it->second.wait_cond, &mutex);
                };
                it->second.state = ClientCacheLock::LOCKED;
                it->second.owner = pthread_self();
                loop = false;
                break;

            case ClientCacheLock::RELEASING:
                while (it->second.state == ClientCacheLock::RELEASING) {
                    pthread_cond_wait(&it->second.release_cond, &mutex);
                }
                loop = true;
                break;

            default:
                loop = false;
                break;
        }
    } while (loop);
    return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
    bool release_flag = false;
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    {
        ScopedLock _(&mutex);
        it = locks.find(lid);
        if (it == locks.end()) {
            ERR("hey, lock not found");
            return lock_protocol::RETRY;
        }
        switch (it->second.state) {
            case ClientCacheLock::NONE:
                ERR("hey, you do not have the lock");
                break;

            case ClientCacheLock::FREE:
                ERR("releasing a free lock?");
                break;

            case ClientCacheLock::LOCKED:
                it->second.state = ClientCacheLock::FREE;
                pthread_cond_signal(&it->second.wait_cond);
                break;

            case ClientCacheLock::ACQUIRING:
                ERR("relesing non-free locks");
                break;

            case ClientCacheLock::RELEASING:
                release_flag = true;
                break;

            default:
                break;
        }
    }       // scoped lock end
    if (release_flag) {
        int r;
        int ret = cl->call(lock_protocol::release, lid, id, r);
        if (ret != lock_protocol::OK) {
            ERR("release failed");
        };
        it->second.state = ClientCacheLock::NONE;
    }
    pthread_cond_broadcast(&it->second.release_cond);
    return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    bool release_flag = false;
    {
        ScopedLock _(&mutex);
        it = locks.find(lid);
        if (it == locks.end()) {
            ERR("lock not found");
            return lock_protocol::RETRY;
        }
        switch(it->second.state) {
            case ClientCacheLock::NONE:
                it->second.revoke++;
                break;

            case ClientCacheLock::FREE:
                it->second.state = ClientCacheLock::RELEASING;
                release_flag = true;
                break;

            case ClientCacheLock::LOCKED:
                it->second.state = ClientCacheLock::RELEASING;
                break;

            case ClientCacheLock::ACQUIRING:
                it->second.revoke++;
                break;

            case ClientCacheLock::RELEASING:
                it->second.revoke++;
                break;

            default:
                break;
        }
    }       // scoped lock end
    if (release_flag) {
        int r;
        int ret = cl->call(lock_protocol::release, lid, id, r);
        if (ret != lock_protocol::OK) {
            ERR("release failed");
        };
        it->second.state = ClientCacheLock::NONE;
    }
    return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
    ScopedLock _(&mutex);       // make sure acquire enters wait
    map<lock_protocol::lockid_t, ClientCacheLock>::iterator it;
    it = locks.find(lid);
    if (it == locks.end()) {
        ERR("retry an empty lock");
    } else {
        pthread_cond_signal(&it->second.retry_cond);
    }
    return lock_protocol::OK;
}



