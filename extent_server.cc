// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "zdebug.h"

/*
    6001:put
    6002:get
    6003:getattr
    6004:remove

 RPC number before writing lab5 code
    RPC STATS: 1:2 6001:802 6002:1451 6003:801 6004:198 

 stat after lab5 finished:
    RPC STATS: 1:2 6001:3 6002:277 6003:2 

*/

extent_server::extent_server() {
    int r;
    pthread_mutex_init(&mutex, NULL);
    put(1, "", r);
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  // You fill this in for Lab 2.
  Z("extent_server put : %llx %s", id, buf.c_str());
  ScopedLock _(&mutex);
  extent_protocol::attr a;
  a.mtime = a.ctime = a.atime = time(NULL);
  a.size = buf.length();
  if (exts.find(id) != exts.end()) {
    a.atime = exts[id].attribute.atime;
  }
  exts[id].name = buf;
  exts[id].attribute = a;
  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  Z("extent_server get : %llx", id);
  ScopedLock _(&mutex);
  // You fill this in for Lab 2.
  if (exts.find(id) == exts.end()) {
    ERR("NOENT found %llx", id);
    return extent_protocol::NOENT;
  }
  buf = exts[id].name;
  exts[id].attribute.atime = time(NULL);
  return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You fill this in for Lab 2.
  ScopedLock _(&mutex);
  Z("extent_server getattr : %llx", id);
  if (exts.find(id) == exts.end()) {
      ERR("NOENT found %llx", id);
      return extent_protocol::NOENT;
  }
  a = exts[id].attribute;
  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  // You fill this in for Lab 2.
  Z("extent_server remove : %llx", id);
  ScopedLock _(&mutex);
  std::map<extent_protocol::extentid_t, Extent>::iterator it;
  it = exts.find(id);
  if (it == exts.end()) {
    return extent_protocol::NOENT;
  }
  exts.erase(it);
  return extent_protocol::OK;
}

