#include <boost/dynamic_bitset.hpp>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include "state.hh"

struct page {
  uint32_t va;
  uint8_t data[4096];
} __attribute__((packed));

struct header {
  static const uint64_t magic = 0xbeefcafefacebabe;
  uint32_t pc;
  int32_t gpr[32];
  int32_t lo;
  int32_t hi;
  uint32_t cpr0[32];
  uint32_t cpr1[32];
  uint32_t fcr1[5];
  uint64_t icnt;
  uint32_t num_nz_pages;
  header() {}
} __attribute__((packed));

void dumpState(const state_t &s, const std::string &filename) {
  static const int n_pages = 1<<20;
  header h;
  boost::dynamic_bitset<> nz_pages(n_pages,false);
  uint64_t *mem64 = reinterpret_cast<uint64_t*>(s.mem);
  static_assert(sizeof(page)==4100, "struct page has weird size");
  
  /* mark non-zero pages */
  for(int p = 0; p < n_pages; p++) {
    for(int pp = 0; pp < 512; pp++) {
      if(mem64[p*512+pp]) {
	nz_pages[p] = true;
	break;
      }
    }
  }
  int fd = ::open(filename.c_str(), O_RDWR|O_CREAT|O_TRUNC, 0600);
  assert(fd != -1);
  h.pc = s.pc;
  memcpy(&h.gpr,&s.gpr,sizeof(s.gpr));
  h.lo = s.lo;
  h.hi = s.hi;
  memcpy(&h.cpr0,&s.cpr0,sizeof(s.cpr0));
  memcpy(&h.cpr1,&s.cpr1,sizeof(s.cpr1));
  memcpy(&h.fcr1,&s.fcr1,sizeof(s.fcr1));
  h.icnt = s.icnt;
  h.num_nz_pages = nz_pages.count();
  ssize_t wb = write(fd, &h, sizeof(h));
  assert(wb == sizeof(h));

  for(size_t i = nz_pages.find_first(); i != boost::dynamic_bitset<>::npos;
      i = nz_pages.find_next(i)) {
    page p;
    p.va = i*4096;
    memcpy(p.data, s.mem+p.va, 4096);
    wb = write(fd, &p, sizeof(p));
    assert(wb == sizeof(p));
  }
  close(fd);
}

void loadState(state_t &s, const std::string &filename) {
  int fd = ::open(filename.c_str(), O_RDONLY, 0600);
  assert(fd != -1);
  header h;
  size_t sz = read(fd, &h, sizeof(h));
  assert(sz == sizeof(h));
  
  s.pc = h.pc;
  memcpy(&s.gpr,&h.gpr,sizeof(s.gpr));
  s.lo = h.lo;
  s.hi = h.hi;
  memcpy(&s.cpr0,&h.cpr0,sizeof(s.cpr0));
  memcpy(&s.cpr1,&h.cpr1,sizeof(s.cpr1));
  memcpy(&s.fcr1,&h.fcr1,sizeof(s.fcr1));
  s.icnt = h.icnt;
  
  for(uint32_t i = 0; i < h.num_nz_pages; i++) {
    page p;
    sz = read(fd, &p, sizeof(p));
    assert(sz == sizeof(p));
    memcpy(s.mem+p.va, p.data, 4096);
  }
  close(fd);
}
