#include <cassert>
#include <cstring>

#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cxxabi.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

#ifdef __amd64__
#include <x86intrin.h>
#endif

#include "helper.hh"


#define	_SIM_FOPEN		(-1)	
#define	_SIM_FREAD		0x0001	
#define	_SIM_FWRITE		0x0002	
#define	_SIM_FAPPEND	        0x0008	
#define	_SIM_FMARK		0x0010
#define	_SIM_FDEFER		0x0020
#define	_SIM_FASYNC		0x0040
#define	_SIM_FSHLOCK	        0x0080
#define	_SIM_FEXLOCK	        0x0100
#define	_SIM_FCREAT		0x0200
#define	_SIM_FTRUNC		0x0400
#define	_SIM_FEXCL		0x0800
#define	_SIM_FNBIO		0x1000
#define	_SIM_FSYNC		0x2000
#define	_SIM_FNONBLOCK	        0x4000
#define	_SIM_FNDELAY	        _SIM_FNONBLOCK
#define	_SIM_FNOCTTY	        0x8000	

#define	O_SIM_RDONLY	0		/* +1 == FREAD */
#define	O_SIM_WRONLY	1		/* +1 == FWRITE */
#define	O_SIM_RDWR	2		/* +1 == FREAD|FWRITE */
#define	O_SIM_APPEND	_SIM_FAPPEND
#define	O_SIM_CREAT	_SIM_FCREAT
#define	O_SIM_TRUNC     _SIM_FTRUNC
#define	O_SIM_EXCL      _SIM_FEXCL
#define O_SIM_SYNC	_SIM_FSYNC
#define	O_SIM_NONBLOCK	_SIM_FNONBLOCK
#define	O_SIM_NOCTTY	_SIM_FNOCTTY
#define	O_SIM_ACCMODE	(O_SIM_RDONLY|O_SIM_WRONLY|O_SIM_RDWR)

static const int32_t simIOFlags[] = 
  {O_SIM_RDONLY,
   O_SIM_WRONLY,
   O_SIM_RDWR,
   O_SIM_APPEND,
   O_SIM_CREAT,
   O_SIM_TRUNC,
   O_SIM_EXCL,
   O_SIM_SYNC,
   O_SIM_NONBLOCK,
   O_SIM_NOCTTY
  };

static const int32_t hostIOFlags[] = 
  {
    O_RDONLY,
    O_WRONLY,
    O_RDWR,
    O_APPEND,
    O_CREAT,
    O_TRUNC,
    O_EXCL,
    O_SYNC,
    O_NONBLOCK,
    O_NOCTTY
  };


#ifdef __amd64__
__attribute__ ((__target__ ("sse4.2"))) 
uint32_t update_crc(uint32_t crc, uint8_t *buf, size_t len) {
  uint32_t c = crc;
  for(size_t n=0;n<len;n++) {
    c = _mm_crc32_u8(c, buf[n]);
  }
  return c;
}
#else
uint32_t update_crc(uint32_t crc, uint8_t *buf, size_t len) {
  /* http://stackoverflow.com/questions/29174349/mm-crc32-u8-gives-different-result-than-reference-code */
  static const uint32_t POLY = 0x82f63b78;
  uint32_t c = crc;  
  for(size_t n=0;n<len;n++) {
    uint8_t b = buf[n];
    c ^= b;
    for(int k = 0; k < 8; k++) {
      c = c & 1 ? (c>>1) ^ POLY : c>>1;
    }
  }
  return c;
}
#endif  


uint32_t crc32(uint8_t *buf, size_t len) {
  return update_crc(~0x0, buf, len) ^ (~0x0);
}

int32_t remapIOFlags(int32_t flags) {
  int32_t nflags = 0;
  for(size_t i = 0; i < sizeof(simIOFlags)/sizeof(simIOFlags[0]); i++) {
    if(flags & simIOFlags[i])
      nflags |= hostIOFlags[i];
  }
  return nflags;
}

double timestamp() {
  struct timeval t;
  gettimeofday(&t,nullptr);
  return timeval_to_sec(t);
}

std::string gethostname() {
  char buf[80];
  int rc = gethostname(buf,sizeof(buf)/sizeof(buf[0]));
  assert(rc==0);
  return std::string(buf);
}

std::string strip_path(const char* str) {
  ssize_t len = static_cast<ssize_t>(strlen(str));
  ssize_t i = (len-1);
  while(i >=0) {
    if(str[i] == '/') {
      i++;
      break;
    }
    --i;
  }
  return std::string(str+i);
}

/* copied from http://eli.thegreenplace.net/2015/programmatic-access-to-the-call-stack-in-c/ */
void dbt_backtrace() {
  unw_cursor_t cursor;
  unw_context_t context;
  
  // Initialize cursor to current frame for local unwinding.
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);
  
  // Unwind frames one by one, going up the frame stack.
  char sym[256] = {0};
  while (unw_step(&cursor) > 0) {
    unw_word_t offset, pc;
    unw_get_reg(&cursor, UNW_REG_IP, &pc);
    if (pc == 0) {
      break;
    }
    if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
      char* nameptr = sym;
      int status;
      char* demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
      if (status == 0) {
        nameptr = demangled;
      }
      std::cerr << nameptr << "+0x" << std::hex << offset << std::dec << "\n";
      std::free(demangled);
    }
    else {
      std::cerr << "-- error: unable to obtain symbol name for this frame\n";
    }
  }
}

extern "C" {
  void print_double(double x) {
    std::cout << "x (double) = " << x << "\n";
  }
  void print_double2(double x, double y) {
    std::cout << "x (double) = " << x << ", y (double) = " << y << "\n";
  }
  void print_float(float x) {
    std::cout << "x (float) = " << x << "\n";
  }
  void print_float2(float x, float y) {
    std::cout << "x (float) = " << x << ", y (float) = " << y << "\n";
  }
  void print_int32(int32_t x) {
    std::cout << "x (int32) = " << x << "\n";
  }
}
