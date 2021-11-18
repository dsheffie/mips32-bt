#ifndef __DEBUG_SYM_HH__
#define __DEBUG_SYM_HH__

#include <cstdint>  // for uint32_t
#include <string>   // for string

#ifdef ENABLE_DEBUG
#include <bfd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

class debugSymDB {
public:
  static void init(const char *binary);
  static debugSymDB* getInstance();
  static void lookup(uint32_t addr, std::string &s);
  static void release();
private:
  struct dbentry_t {
    uint32_t line;
    char* file;
    char* func;
  };
  bfd *b = nullptr;
  bool initd = false;
  asymbol **syms = nullptr;
  ~debugSymDB();
  void buildDB();
  std::map<uint32_t, dbentry_t> dbgMap;
  void plookup(uint32_t addr, std::string &s);
  void pinit(const char *exe);
  static debugSymDB *theInstance;
  debugSymDB();
};
#else
class debugSymDB {
public:
  static void init(const char *binary);
  static debugSymDB* getInstance();
  static void lookup(uint32_t addr, std::string &s);
  static void release();
};
#endif

#endif
