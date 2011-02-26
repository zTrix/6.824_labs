#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>


#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"

typedef std::string reqid_t;
class lock_server_cache {
 private:
  int nacquire;
  enum lstate {
    FREE,
    OWNED,
    REVOKING
  };
  struct lock {
    lock(lock_protocol::lockid_t _lid) { 
      lid = _lid; 
      state = FREE;
    }
    std::list<reqid_t> requests;
    lock_protocol::lockid_t lid;
    lstate state;
  };
  std::map<lock_protocol::lockid_t, lock*> locks;
  pthread_mutex_t server_mutex;
  pthread_cond_t revoke_cond;
 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void dorevoke(lock *l);
  void doretry(lock *l);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
