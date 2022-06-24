#ifndef __STATE_HH__
#define __STATE_HH__


struct state_t;

#ifndef ELIDE_STATE_IMPL
#include <cstdint>
#include <iostream>

struct state_t {
  uint32_t pc;
  int32_t gpr[32];
  int32_t lo;
  int32_t hi;
  uint32_t cpr0[32];
  uint32_t cpr1[32];
  uint32_t fcr1[5];
  uint64_t icnt;
  uint8_t *mem;
  uint64_t abortloc;
  uint8_t brk;
  uint32_t oldpc;
};

std::ostream &operator<<(std::ostream &out, const state_t & s);
void initState(state_t *s);
#endif

#endif
