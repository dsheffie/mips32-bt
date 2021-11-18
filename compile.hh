#ifndef __COMP_INST_HH__
#define __COMP_INST_HH__
#include <cstdint>
class compile {
public:
  static bool canCompileInstr(uint32_t inst);
  static bool canCompileRType(uint32_t inst);
  static bool canCompileJType(uint32_t inst);
  static bool canCompileIType(uint32_t inst);
  static bool canCompileSpecial2(uint32_t inst);
  static bool canCompileSpecial3(uint32_t inst);
  static bool canCompileCoproc1(uint32_t inst);
  static bool canCompileCoproc1x(uint32_t inst);
};
#endif
