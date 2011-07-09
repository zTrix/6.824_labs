// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client.h"
#include "lock_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include "zdebug.h"

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
    ec = new extent_client(extent_dst);
    elr = new ExtentLockRelease(ec);
    lc = new lock_client_cache(lock_dst, elr);
    srand(time(NULL) + getpid());
}

yfs_client::inum yfs_client::rand_inum(bool isfile) {
    inum ret = 0;
    ret = (unsigned long long) ( (rand() & 0x7fffffff) | (isfile << 31) );
    ret &= 0xffffffff;
    return ret;
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  ScopedLockClient _(lc, inum);
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    ERR("getattr error");
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  ScopedLockClient _(lc, inum);
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

int yfs_client::create(inum parent, const char * name, bool isfile, unsigned long &ino) {
    Z("create : parentis %llx name is %s\n", parent, name);
    if (isdir(parent)) {
        std::string b;
        ScopedLockClient slc(lc, parent);
        int rs = ec->get(parent, b);
        if (rs != OK) {
            ERR("get parent failed");
            return rs;
        }
        std::string t = "/" + std::string(name) + "/";
        if (b.find(t) != std::string::npos) {
            ERR("create file exist ");
            return EXIST;
        }
        inum num = rand_inum(isfile);
        ino = (unsigned long)(num & 0xffffffff);
        b = b.append(filename(num) + t);
        ScopedLockClient _(lc, num);
        rs = ec->put(num, "");
        if (rs != OK) {
            ERR("create file, put operation failed");
            return rs;
        }
        rs = ec->put(parent, b);
        if (rs != OK) {
            ERR("update parent failed");
            return rs;
        }
        return OK;
    }
    return NOENT;
}

bool yfs_client::lookup(inum parent, const char *name, unsigned long &ino) {
    Z("parent %llx name '%s'\n", parent, name);
    if (isdir(parent)) {
        //printf("%d %d \n", name == NULL, strlen(name));
        if (name == NULL || strlen(name) < 1) {
            ERR("lookup null");
            return true;
        }
        std::string b;
        ScopedLockClient slc(lc, parent);
        int rs = ec->get(parent, b);
        if (rs != OK) {
            ERR("look up parent failed");
            return false;
        } else {
            Z("%llx content: %s", parent, b.c_str());
        }
        std::string t = "/" + std::string(name) + "/";
        size_t found = b.find(t);
        if (found != std::string::npos) {
            assert(found > 0);
            size_t left = b.rfind('/', found - 1);
            if (left == std::string::npos) {
                left = 0;
            } else {
                left++;
            }
            assert(found > left);
            ino = n2i(b.substr(left, found - left));
            
            return true;
        }
    }
    return false;
}

int yfs_client::read(inum ino, size_t size, off_t off, std::string &ret) {
    Z("read: size = %lu, off = %ld", size, off);
    std::string buf;
    ScopedLockClient _(lc, ino);
    int rs = ec->get(ino, buf);
    if (rs != OK) {
        ERR("extent client get rs not OK: %d", rs);
        return rs;
    }
    if (off + size > buf.size()) {
        int reside = off + size - buf.size();
        char * a = new char[reside];
        bzero(a, off + size - buf.size());
        ret = buf.substr(off, buf.size() - off).append(std::string(a, reside));
    } else {
        ret = buf.substr(off, size);
    }
    //Z("read result = %s", ret.c_str());
    return OK;
}

int yfs_client::write(inum ino, const char * buf, size_t size, off_t off) {
    if (off < 0) {
        return NOENT;
    }
    size_t uoff = (unsigned)off;
    std::string ori;
    ScopedLockClient slc(lc, ino);
    int rs = ec->get(ino, ori);
    if (rs != OK) {
        return rs;
    }
    std::string after;
    if (uoff <= ori.size() || !uoff) {
        after = ori.substr(0, uoff).append(std::string(buf, size));
        if (uoff + size < ori.size()) {
            after.append(ori.substr(uoff + size, ori.size() - uoff - size));
        }
    } else {
        size_t gap = uoff - ori.size();
        char * a = new char[gap];
        bzero(a, gap);
        after = ori.append(std::string(a, gap));
        after = after.append(std::string(buf, size));
    }
    rs = ec->put(ino, after);
    return rs;
}

int yfs_client::setattr(inum fileno, struct stat *attr) {
    std::string buf;
    ScopedLockClient slc(lc, fileno);
    int rs = ec->get(fileno, buf);
    if (rs != OK) {
        return rs;
    }
    unsigned int sz = buf.size();
    if (sz < attr->st_size) {
        int appendSz = attr->st_size - sz;
        char * a = new char[appendSz];
        bzero(a, appendSz);
        buf.append(std::string(a, appendSz));
    } else {
        buf = buf.substr(0, attr->st_size);
    }
    rs = ec->put(fileno, buf);
    return rs;
}

int yfs_client::unlink(inum parent, const char *name) {
    Z("unlink parent %llx name '%s'\n", parent, name);
    if (isdir(parent)) {
        //printf("%d %d \n", name == NULL, strlen(name));
        if (name == NULL || strlen(name) < 1) {
            ERR("bad param: name");
        }
        std::string b;
        ScopedLockClient slc(lc, parent);
        int rs = ec->get(parent, b);
        if (rs != OK) {
            ERR("rs = %d", rs);
            return NOENT;
        }
        std::string n = std::string(name);
        std::string t = "/" + n + "/";
        size_t found = b.find(t);
        if (found != std::string::npos) {
            size_t left = b.rfind('/', found - 1);
            if (left == std::string::npos) {
                left = 0;
            } else {
                left++;
            }
            assert(found > left);
            inum ino = n2i(b.substr(left, found - left));
            ScopedLockClient slc2(lc, ino);
            rs = ec->remove(ino);
            if (rs != extent_protocol::OK) {
                ERR("remove error");
                return NOENT;
            }
            std::string after = "";
            if (left) {
                after = after.append(b.substr(0, left));
            }
            if (found + t.size() < b.size()) {
                after = after.append(b.substr(found + t.size(), b.size() - found - t.size()));
            }
            rs = ec->put(parent, after);
            if (rs != OK) {
                ERR("put parent failed");
                return NOENT;
            }
            return OK;
        } else {
            ERR("file or dir not found");
        }
    } else {
        ERR("parent is not dir");
    }
    return NOENT;
}

int yfs_client::getdata(inum ino, std::string & buf) {
    ScopedLockClient _(lc, ino);
    return ec->get(ino, buf);
}
