// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
    class ExtentCache {
    public:
        std::string data;
        bool dirty;
        bool valid;     // whether the data is the latest and using
        extent_protocol::attr a;
        ExtentCache() : data(""), dirty(false), valid(false) {}
    };
 private:
  rpcc *cl;
  std::map<extent_protocol::extentid_t, ExtentCache> cache;

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status flush(extent_protocol::extentid_t eid);
};

#endif 

