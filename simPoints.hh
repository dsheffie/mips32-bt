#ifndef __SIMPOINTS_HH__
#define __SIMPOINTS_HH__
#include <cstdint>  // for uint32_t, uint64_t
#include <string>   // for string
extern "C" {
  void log_bb(uint32_t pc,  uint64_t icnt);
}
void save_simpoints_data(const std::string &fname);
#endif
