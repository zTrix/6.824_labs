// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
    srand(time(NULL));
}

yfs_client::inum yfs_client::rand_inum(bool isfile) {
    return rand() & 0x7fffffff | isfile << 31 ;
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

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
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

int yfs_client::put(inum num, std::string buf) {
    extent_protocol::status rs;
    rs = ec->get(num, buf);
    if (rs == extent_protocol::OK) {
        return EXIST;
    }
    rs = ec->put(num, buf);
    if (rs == extent_protocol::OK) {
        return OK;
    }
    return IOERR;
}

int yfs_client::get(inum num, std::string &buf) {
    extent_protocol::status rs = ec->get(num, buf);
    if (rs == extent_protocol::OK) {
        return OK;
    }
    return IOERR;
}

int yfs_client::create(inum num, const char * name) {
    if (isdir(parent)) {
        inum num = rand_inum();
        yfs_client::status rs;
        rs = put(num, name);
        if (rs == EXIST) {
            return rs;
        }
        return yfs_client::OK;
    }
    return yfs_client::NOENT;
}

