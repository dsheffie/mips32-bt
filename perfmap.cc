#include "perfmap.hh"
#include <unistd.h>

perfmap::perfmap() {
  refs = 0; myPid = 0; fp = nullptr;
}

perfmap::~perfmap() {
#ifdef __linux__
  myPid = getpid();
  fname = "perf-" + std::to_string(myPid) + ".map";
  std::string fullpath = "/tmp/" + fname;
  fp = fopen(fullpath.c_str(), "w");
  fprintf(fp, "%s", perfLog.c_str());
  fclose(fp);
#endif

}

void perfmap::relReference() {
  theInstance->refs--;
  if(refs==0) {
    delete theInstance;
    theInstance = nullptr;
  }
}

perfmap *perfmap::getReference() {
  if(theInstance==nullptr) {
    theInstance = new perfmap();
  }
  theInstance->refs++;
  return theInstance;
}

void perfmap::addEntry(uint64_t addr, uint64_t size, std::string &name) {
#ifdef __linux__
  char buf[80] = {0};
  snprintf(buf, 80, "%lx %lx %s\n", addr, size, name.c_str());
  perfLog += std::string(buf);
#endif
}


