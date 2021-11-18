#ifndef __HELPERFUNCTS__
#define __HELPERFUNCTS__
#include <cstddef>
#include <string>
#include <sstream>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <cmath>
#include <limits>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>


static const char KNRM[] = "\x1B[0m";
static const char KRED[] = "\x1B[31m";
static const char KGRN[] = "\x1B[32m";
static const char KYEL[] = "\x1B[33m";
static const char KBLU[] = "\x1B[34m";
static const char KMAG[] = "\x1B[35m";
static const char KCYN[] = "\x1B[36m";
static const char KWHT[] = "\x1B[37m";

void dbt_backtrace();

#define die() {								\
    std::cerr << __PRETTY_FUNCTION__ << " @ " << __FILE__ << ":"	\
	      << __LINE__ << " called die\n";				\
    dbt_backtrace();							\
    abort();								\
  }

#define dbt_assert(cond) {                                              \
    if(!(cond)) {                                                       \
      std::cerr << #cond << " is false @ "				\
		<< __PRETTY_FUNCTION__ << " : line " << __LINE__	\
		<< " in file " << __FILE__				\
		<< std::endl;						\
	dbt_backtrace();						\
	abort();							\
    }                                                                   \
  }

#ifdef __amd64__
#define gdb_break() { __asm__("int $3"); }
#endif

#define print_var(x) {					\
    std::cerr << #x << " = " << x  << std::endl;	\
  }

#define print_var_hex(x) {		      \
    std::cerr << #x << " = " << std::hex << x \
	      << std::dec << std::endl;	      \
  }

#define print_var2(x,y) {						\
    std::cerr << #x << " = " << x << "," << #y << " = " << y << std::endl; \
  }

#define stream_hex(s,x) {s << #x << " = " << std::hex << x << std::dec << std::endl;}

double timestamp();

inline uint64_t rdtsc(void)  {
  uint32_t hi=0, lo=0;
#ifdef __amd64__
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
#endif
  return ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
}

uint32_t update_crc(uint32_t crc, uint8_t *buf, size_t len);
uint32_t crc32(uint8_t *buf, size_t len);

int32_t remapIOFlags(int32_t flags);

template <typename T> std::string toStringHex(T x) {
  std::stringstream ss;
  ss << std::hex << x;
  return ss.str();
}

template <typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
bool extractBit(T x, uint32_t b) {
  return (x >> b) & 0x1;
}

template <typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
T setBit(T x, bool v, uint32_t b) {
  T vv = v ? static_cast<T>(1) : static_cast<T>(0);
  T t = static_cast<T>(1) << b;
  T tt = (~t) & x;
  t  &= ~(vv-1);
  return (tt | t);
}

#define INTEGRAL_ENABLE_IF(SZ,T) typename std::enable_if<std::is_integral<T>::value and (sizeof(T)==SZ),T>::type* = nullptr

template <bool EL, typename T, INTEGRAL_ENABLE_IF(1,T)>
T bswap(T x) {
  return x;
}

template <bool EL, typename T, INTEGRAL_ENABLE_IF(2,T)> 
T bswap(T x) {
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "must be little endian machine");
  if(EL) 
    return x;
  else
  return  __builtin_bswap16(x);
}

template <bool EL, typename T, INTEGRAL_ENABLE_IF(4,T)>
T bswap(T x) {
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "must be little endian machine");
  if(EL)
    return x;
  else 
    return  __builtin_bswap32(x);
}

template <bool EL, typename T, INTEGRAL_ENABLE_IF(8,T)> 
T bswap(T x) {
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "must be little endian machine");
  if(EL)
    return x;
  else 
    return  __builtin_bswap64(x);
}

#undef INTEGRAL_ENABLE_IF


template <typename T>
T max(T x, T y) {
  return (x>y) ? x : y;
}
template <typename T>
T min(T x, T y) {
  return (x<y) ? x : y;
}
template <typename T, typename std::enable_if<std::is_integral<T>::value,T>::type* = nullptr>
bool isPow2(T x) {
  return (((x-1)&x) == 0);
}

std::string gethostname();
std::string strip_path(const char* str);

inline double timeval_to_sec(struct timeval &t) {
  return t.tv_sec + 1e-6 * static_cast<double>(t.tv_usec);
}

inline std::ostream &operator <<(std::ostream &out, struct rusage &usage) {
  out << "user = " << timeval_to_sec(usage.ru_utime) << "s,"
      << "sys = " << timeval_to_sec(usage.ru_stime) << "s,"
      << "mss = " << usage.ru_maxrss << " kbytes";
  return out;
}

template <typename T>
class backtrace_allocator {
public:
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T*        pointer;
  typedef const T*  const_pointer;
  typedef T&        reference;
  typedef const T&  const_reference;
  typedef T         value_type;
  backtrace_allocator() {}
  backtrace_allocator(const backtrace_allocator &) {}
  T *allocate(size_type n, const void * = 0) {
    void *ptr = std::malloc(n*sizeof(T));
    if(ptr==nullptr) {
      die();
    }
    return reinterpret_cast<T*>(ptr);
  }
  void deallocate(void *ptr, size_type) {
    if(ptr != nullptr) {
      std::free(ptr);
    }
  }
};

template <typename T, typename U>
bool operator==(const backtrace_allocator<T>&, const backtrace_allocator<U>&) {
  return true;
}
template <typename T, typename U>
bool operator!=(const backtrace_allocator<T>&, const backtrace_allocator<U>&) {
  return false;
}

#ifndef UNREACHABLE
#define UNREACHABLE() {				\
    __builtin_unreachable();			\
  }
#endif

#endif
