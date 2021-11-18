#ifndef __EXECUNIT_HH__
#define  __EXECUNIT_HH__

#include "state.hh"
#include "helper.hh"
#include <cstdint>
#include <string>
#include <typeinfo>

class basicBlock;

class execUnit {
protected:
  uint64_t inscnt;
public:
  execUnit() : inscnt(0) {}
  virtual basicBlock* run(state_t *s) = 0;
  virtual void info() = 0;
  virtual void report(std::string &s, uint64_t icnt) {}
  virtual uint32_t getEntryAddr() const = 0;
  virtual ~execUnit() {}
  uint64_t getInscnt() const {
    return inscnt;
  }
  class execUnitSorter {
  public:
    bool operator() (const execUnit *x, const execUnit *y) const {
      return x->inscnt > y->inscnt;
    }
  };
};



#endif
