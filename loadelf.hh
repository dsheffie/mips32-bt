#include <list>

#ifndef __LOAD_ELF_HH__
#define __LOAD_ELF_HH__

bool load_elf(const char* fn, uint32_t &entry_p, std::map<uint32_t, std::pair<std::string, uint32_t>> &syms, uint8_t *mem);

#endif 

