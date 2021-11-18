#include <fstream>    
#include <map>        
#include <utility>
#include "simPoints.hh"
#define ELIDE_LLVM
#include "globals.hh" 

static uint64_t last_icnt = 0, last_slice = 0;
typedef std::map<uint32_t,uint64_t> slice_t;
static std::map<uint64_t, slice_t> simLog;

extern "C" void log_bb(uint32_t pc,  uint64_t icnt) {
  if(icnt >= (last_slice+globals::simPointsSlice)) {
    last_slice = icnt;
  }
  simLog[last_slice][pc] += (icnt-last_icnt);
  last_icnt = icnt;
}

void save_simpoints_data(const std::string &fname) {
  std::ofstream out(fname);
  for(const auto &p : simLog) {
    for(const auto &pp : p.second) {
      out << p.first << "," << pp.first << "," << pp.second << "\n";
    }
  }

  out.close();
}
