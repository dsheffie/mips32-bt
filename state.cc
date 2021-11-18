#include <cstring>
#include "state.hh"
#include "disassemble.hh"

std::ostream &operator<<(std::ostream &out, const state_t & s) {
  using namespace std;
  out << "PC : " << hex << s.pc << dec << "\n";
  for(int i = 0; i < 32; i++) {
    out << getGPRName(i) << " : 0x"
	<< hex << s.gpr[i] << dec
	<< "(" << s.gpr[i] << ")\n";
  }
  for(int i = 0; i < 32; i++) {
    out << "cpr0_" << i << " : "
	<< hex << s.cpr0[i] << dec
	<< "\n";
  }
  for(int i = 0; i < 32; i++) {
    out << "cpr1_" << i << " : "
	<< hex << s.cpr1[i] << dec
	<< "\n";
  }
  for(int i = 0; i < 5; i++) {
    out << "fcr" << i << " : "
	<< hex << s.fcr1[i] << dec
	<< "\n";
  }
  out << "icnt : " << s.icnt << "\n";
  return out;
}


void initState(state_t *s){
  memset(s, 0, sizeof(state_t));
  /* setup the status register */
  s->cpr0[12] |= ((1<<2) | (1<<22));
}


