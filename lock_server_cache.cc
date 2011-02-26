// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
  VERIFY(pthread_mutex_init(&server_mutex, NULL) == 0);
  VERIFY(pthread_cond_init(&revoke_cond, NULL) == 0);  
}


// caller must hold server_mutex
void
lock_server_cache::dorevoke(lock *l)
{
}


// caller should hold server_mutex
void
lock_server_cache::doretry(lock *l)
{
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
             int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

