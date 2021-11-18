#include "debugSymbols.hh"
#include <string>  // for string

#ifdef ENABLE_DEBUG
debugSymDB *debugSymDB::theInstance = nullptr;

debugSymDB::debugSymDB() {
  b = 0;
  syms = 0;
  initd = false;
}

debugSymDB::~debugSymDB() {
#ifdef __linux__
  if(initd) {
    free(syms);
    bfd_close(b);
  }
  for(std::map<uint32_t, dbentry_t>::iterator mit = dbgMap.begin();
      mit != dbgMap.end(); mit++) {
    dbentry_t &d = mit->second;
    free(d.file);
    free(d.func);
  }
#endif
  theInstance = 0;
}

debugSymDB *debugSymDB::getInstance() {
#ifdef __linux__
  if(theInstance == 0) {
    theInstance = new debugSymDB();
  }
#endif
  return theInstance;
}

/* adapted from binutils addr2line */
void debugSymDB::pinit(const char *exe) {
#ifdef __linux__
  long storage=0,symcount=0;
  bool dynamic = false;
  char **matching = 0;
  if(initd)
    return;
  b = bfd_openr(exe, nullptr);
  if(b==nullptr) {
    printf("=>bfd: couldn't open %s<=\n", exe);
    exit(-1);
  }
  b->flags |= BFD_DECOMPRESS;
  if(bfd_check_format(b, bfd_archive)) {
    printf("=>bfd: bad format<=\n");
    exit(-1);
  }
  /* This is required */
  if(!bfd_check_format_matches(b, bfd_object, &matching)) {
    exit(-1);
  }
  if((bfd_get_file_flags(b)&HAS_SYMS) == 0) {
    printf("=>bfd: no symbols<=\n");
    exit(-1);
  }
  storage = bfd_get_symtab_upper_bound(b);
  if(storage == 0) {
    storage = bfd_get_dynamic_symtab_upper_bound(b);
    dynamic = true;
  }
  if(storage < 0) {
    printf("=>bfd: storage less than zero<=\n");
    exit(-1);
  }
  syms = (asymbol**)malloc(storage);
  if(dynamic) 
    symcount = bfd_canonicalize_dynamic_symtab (b, syms);
  else 
    symcount = bfd_canonicalize_symtab (b, syms);

  if(symcount <= 0) {
    printf("=>bfd: found zero symbols<=\n");
    exit(-1);
  }
  buildDB();
#endif
  initd=true;
}

void debugSymDB::lookup(uint32_t addr, std::string &s) {
  return theInstance->plookup(addr,s);
}
void debugSymDB::init(const char *exe) {
#ifdef __linux__
  debugSymDB *d = getInstance();
  if(!d->initd)
    d->pinit(exe);
#endif
}

void debugSymDB::release() {
#ifdef __linux__
  delete theInstance;
  theInstance = 0;
#endif
}

void debugSymDB::plookup(uint32_t addr, std::string &s) {
#ifdef __linux__
  std::map<uint32_t,dbentry_t>::iterator mit = dbgMap.find(addr);
  if(mit != dbgMap.end()) {
    dbentry_t &d= mit->second;
    std::stringstream ss;
    ss << "func " << d.func << " @ line " << d.line << "\n";
    s += ss.str();
  }
#endif
}

void debugSymDB::buildDB() {
#ifdef __linux__
  for(asection *p = b->sections; p != nullptr; p=p->next) {
    if((bfd_section_flags(p) & SEC_ALLOC) == 0)
      continue;
    bfd_size_type s = bfd_section_size(p);
    bfd_vma vma = bfd_section_vma(p);
    for(uint32_t pc = 0; pc < s; pc+=4) {
      const char *file=0;
      const char *func=0;
      uint32_t line;
      bfd_find_nearest_line(b, p, syms, pc, &file, &func, &line);
      if(func) {
	char *funcstr=0, *filestr=0;
	funcstr = func ? strdup(func) : strdup("???");
	filestr = file ? strdup(file) : strdup("???");
	dbentry_t e{line, filestr, funcstr};
	dbgMap[vma+pc] = e;
      }
    }
  }
#endif
}


#else

void debugSymDB::init(const char *binary) {
  return;
}

debugSymDB* debugSymDB::getInstance() {
  return nullptr;
}

void debugSymDB::lookup(uint32_t addr, std::string &s) {
  return;
}

void debugSymDB::release() {
  return;
}


#endif
