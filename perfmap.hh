#ifndef __PERFMAP__
#define __PERFMAP__

#include <sys/types.h>
#include <cstdint>
#include <cstdio>
#include <string>

/* interface to linux perf subsystem */
class perfmap {
public:
  static perfmap *getReference();
  void relReference();
  void addEntry(uint64_t addr, uint64_t size, std::string &name);
private:
  static perfmap *theInstance;
  std::string perfLog;
  perfmap();
  ~perfmap();
  std::string fname;
  size_t refs;
  FILE *fp;
  pid_t myPid;
};

#endif
