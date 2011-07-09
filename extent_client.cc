// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "zdebug.h"

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
    extent_protocol::status ret = extent_protocol::OK;
    bool flag;
    if (!(flag=cache.count(eid)) || (!cache[eid].valid && !cache[eid].dirty)) {
        ret = cl->call(extent_protocol::get, eid, buf);
        if (ret == extent_protocol::OK) {
            Z("call get, data = %s", buf.c_str());
            cache[eid].data = buf;
            cache[eid].dirty = false;
            cache[eid].valid = true;
            cache[eid].a.atime = time(NULL);
            if (!flag) {
                cache[eid].a.ctime = cache[eid].a.mtime = 0;
                cache[eid].a.size = buf.size();
            }
        } else {
            ERR("call get failed, ret = %d", ret);
            return ret;
        }
    };
    buf = cache[eid].data;
    Z("get data = %s", buf.c_str());
    return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
    extent_protocol::status ret = extent_protocol::OK;
    extent_protocol::attr *a;
    if (!cache.count(eid)) {
        a = &cache[eid].a;
        a->atime = a->ctime = a->mtime = a->size = 0;
        cache[eid].valid = false;
        cache[eid].dirty = false;
    };
    a = &cache[eid].a;
    if (!a->ctime || !a->mtime || !a->atime) {
        extent_protocol::attr temp;
        ret = cl->call(extent_protocol::getattr, eid, temp);
        if (ret == extent_protocol::OK) {
            if (!a->ctime) {
                a->ctime = temp.ctime;
            }
            if (!a->mtime) {
                a->mtime = temp.mtime;
            }
            if (!a->atime) {
                a->atime = temp.atime;
            }
            attr = *a;
        } else {
            ERR("call getattr failed");
        }
    } else {
        attr = *a;
    }
    return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
    extent_protocol::status ret = extent_protocol::OK;

    extent_protocol::attr a;
    a.mtime = a.ctime = a.atime = time(NULL);
    a.size = buf.size();
    if (cache.count(eid)) {
        a.atime = cache[eid].a.atime;
        if (!cache[eid].valid) {
            ERR("puting on invalid cache");
        }
    }
    cache[eid].data = buf;
    cache[eid].dirty = true;
    cache[eid].a = a;
    cache[eid].valid = true;
    return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
    extent_protocol::status ret = extent_protocol::OK;
    if (!cache.count(eid)) {
        extent_protocol::attr &a = cache[eid].a;
        a.atime = a.ctime = a.mtime = a.size = 0;
        ERR("NOENT");
        return extent_protocol::NOENT;
    }
    cache[eid].valid = false;
    cache[eid].dirty = true;
    return ret;
}

extent_protocol::status
extent_client::flush(extent_protocol::extentid_t eid) {
    extent_protocol::status ret = extent_protocol::OK;
    if (!cache.count(eid)) {
        ERR("flushing non-existing eid %llx", eid);
        return ret;
    }
    int r;
    if (cache[eid].dirty) {
        if (!cache[eid].valid) {
            Z("flush: remove %llx", eid);
            ret = cl->call(extent_protocol::remove, eid, r);
            if (ret != extent_protocol::OK) {
                ERR("call remove failed, ret is %d", ret);
            }
        } else {
            Z("flush: update %llx with data: %s", eid, cache[eid].data.c_str());
            ret = cl->call(extent_protocol::put, eid, cache[eid].data, r);
            if (ret != extent_protocol::OK) {
                ERR("call put failed, rs = %d", ret);
            }
        }
    } else {
        Z("nothing to flush");
    }
    cache[eid].valid = false;
    cache[eid].dirty = false;
    extent_protocol::attr &a = cache[eid].a;
    a.atime = a.mtime = a.ctime = a.size = 0;
    return ret;
}


