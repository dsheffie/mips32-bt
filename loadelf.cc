#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <utility>
#include <cstdint>
#include <map>
#include <array>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_MAC
#include <libelf/gelf.h>
#endif /* TARGET_OS_MAC */
#else
#include <elf.h>
#endif


#include "helper.hh"
#define ELIDE_LLVM
#include "globals.hh"


static const void* FAILED_MMAP = reinterpret_cast<void*>(-1);

static const std::array<uint8_t,4> magicArr = {0x7f, 'E', 'L', 'F'};

#define BS(x) (globals::isMipsEL ? bswap<true>(x) : bswap<false>(x))

static bool checkElf(const Elf32_Ehdr *eh32) {
  auto id = reinterpret_cast<const uint8_t*>(eh32->e_ident);
  for(size_t i = 0, len = magicArr.size(); i < len; i++) {
    if(id[i] != magicArr[i]) return false;
  }
  return true;
}

static bool check32Bit(const Elf32_Ehdr *eh32) {
  return (eh32->e_ident[EI_CLASS] == ELFCLASS32);
}


bool load_elf(const char* fn, uint32_t &entry_p,  std::map<uint32_t, std::pair<std::string, uint32_t>> &syms, uint8_t *mem){
  struct stat s;
  Elf32_Ehdr *eh32 = nullptr;
  Elf32_Phdr* ph32 = nullptr;
  Elf32_Shdr* sh32 = nullptr;
  int32_t e_phnum=-1,e_shnum=-1;
  void *mm = nullptr;
  uint8_t *buf = nullptr;
  int fd = open(fn, O_RDONLY);
  if(fd<0) {
    std::cerr << globals::binaryName << ": open() returned " << fd << "\n";
    return false;
  }
  if(fstat(fd,&s) < 0) {
    std::cerr << globals::binaryName << ": fstat() failed\n";
    return false;
  }
  mm = mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if(mm == FAILED_MMAP) {
    std::cerr << globals::binaryName << ": mmap failed\n";    
    return false;
  }
  eh32 = reinterpret_cast<Elf32_Ehdr*>(mm);
  if(!checkElf(eh32) || !check32Bit(eh32)) {
    std::cerr << globals::binaryName << ": bogus binary\n";        
    return false;
  }
  buf = reinterpret_cast<uint8_t*>(mm);
  /* Check for a MIPS machine */
  if(eh32->e_ident[EI_DATA] == ELFDATA2LSB) {
    globals::isMipsEL = true;
  }
  else if(eh32->e_ident[EI_DATA] == ELFDATA2MSB) {
    globals::isMipsEL = false;
  }
  else {
    die();
  }
  
  if(BS(eh32->e_machine) != 8) {
    std::cerr << globals::binaryName << ": non-mips binary..goodbye\n";
    return false;
  }
  entry_p = BS(eh32->e_entry);

  e_phnum = BS(eh32->e_phnum);
  ph32 = reinterpret_cast<Elf32_Phdr*>(buf + BS(eh32->e_phoff));
  e_shnum = BS(eh32->e_shnum);
  sh32 = reinterpret_cast<Elf32_Shdr*>(buf + BS(eh32->e_shoff));
  
  uint32_t lAddr = entry_p;

  /* Find instruction segments and copy to
   * the memory buffer */
  for(int32_t i = 0; i < e_phnum; i++, ph32++) {
    int32_t p_memsz = BS(ph32->p_memsz);
    int32_t p_offset = BS(ph32->p_offset);
    int32_t p_filesz = BS(ph32->p_filesz);
    int32_t p_type = BS(ph32->p_type);
    uint32_t p_vaddr = BS(ph32->p_vaddr);
    if(p_type == SHT_PROGBITS && p_memsz) {
      if( (p_vaddr + p_memsz) > lAddr) {
	lAddr = (p_vaddr + p_memsz);
      }
      memset(mem+p_vaddr, 0, sizeof(uint8_t)*p_memsz);
      memcpy(mem+p_vaddr, reinterpret_cast<uint8_t*>(buf + p_offset),
	     sizeof(uint8_t)*p_filesz);
    }
  }
  Elf32_Sym *SymTbl = nullptr;
  ssize_t SymTblEntries = -1;
  uint8_t *strtab = nullptr;
  
  for(int32_t i = 0; i < e_shnum; i++, sh32++) {
    uint32_t type = BS(sh32->sh_type);
    if(type == SHT_SYMTAB) {
      SymTblEntries = BS(sh32->sh_size) / sizeof(Elf32_Sym);
      SymTbl = reinterpret_cast<Elf32_Sym*>(buf + BS(sh32->sh_offset));
    }
    if(type == SHT_STRTAB && SymTbl) {
      uint32_t uoffset = BS(sh32->sh_offset);
      strtab = buf+uoffset;
    }
  }
  if(SymTbl) {
    for(ssize_t i = 0; i < SymTblEntries; ++i) {
      uint32_t type = BS(SymTbl->st_info) & 0xf;
      uint32_t size = BS(SymTbl->st_size);
      if(type == 2 && size != 0) {
	uint32_t addr = BS(SymTbl->st_value);
	uint32_t name = BS(SymTbl->st_name);
	char *func_name = reinterpret_cast<char*>(strtab + name);
	syms[addr] = std::pair<std::string,uint32_t>(std::string(func_name),size);
      }
      SymTbl++;
    }
  }
  munmap(mm, s.st_size); 
  close(fd);
  return true;
}


