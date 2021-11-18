#ifndef __MONITOR_HH__
#define __MONITOR_HH__

#include <sys/time.h>
#include <sys/times.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static const uint32_t K1SIZE = 0x80000000;

struct timeval32_t {
  uint32_t tv_sec;
  uint32_t tv_usec;
}; 

struct tms32_t {
  uint32_t tms_utime;
  uint32_t tms_stime;
  uint32_t tms_cutime;
  uint32_t tms_cstime;
};

struct stat32_t {
  uint16_t st_dev;
  uint16_t st_ino;
  uint32_t st_mode;
  uint16_t st_nlink;
  uint16_t st_uid;
  uint16_t st_gid;
  uint16_t st_rdev;
  uint32_t st_size;
  uint32_t _st_atime;
  uint32_t st_spare1;
  uint32_t _st_mtime;
  uint32_t st_spare2;
  uint32_t _st_ctime;
  uint32_t st_spare3;
  uint32_t st_blksize;
  uint32_t st_blocks;
  uint32_t st_spare4[2];
};

static void getNextBlock(state_t *s);


template<bool EL>
void _monitorBody(uint32_t inst, state_t *s) {
  uint32_t reason = ((inst >> RSVD_INSTRUCTION_ARG_SHIFT) & RSVD_INSTRUCTION_ARG_MASK) >> 1;
  switch(reason)
    {
    case 6: {
      /* int open(char *path, int flags) */
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a0);
      const char *path = (char*)(s->mem + uptr);
      int flags = remapIOFlags(s->gpr[R_a1]);
      s->gpr[R_v0] = open(path, flags, S_IRUSR|S_IWUSR);
      break;
    }
    case 7: {
      /* int read(int file,char *ptr,int len) */
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a1);
      s->gpr[R_v0] = read(s->gpr[R_a0], (char*)(s->mem + uptr), s->gpr[R_a2]);
      break;
    }
    case 8: { 
      /* int write(int file, char *ptr, int len) */
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a1);
      s->gpr[R_v0] = (int32_t)write(s->gpr[R_a0], (void*)(s->mem + uptr), s->gpr[R_a2]);
      break;
    }
    case 9: /* lseek */
      s->gpr[R_v0] = lseek(s->gpr[R_a0], s->gpr[R_a1], s->gpr[R_a2]);
      break;
    case 10: /* close */
      if(s->gpr[R_a0]>2)
	s->gpr[R_v0] = (int32_t)close(s->gpr[R_a0]);
      else
	s->gpr[R_v0] = 0;
      break;
    case 13: { /* fstat */
      struct stat native_stat;
      stat32_t *host_stat = nullptr;
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a1);
      s->gpr[R_v0] = fstat(s->gpr[R_a0], &native_stat);
      host_stat = reinterpret_cast<stat32_t*>(s->mem + uptr); 

      host_stat->st_dev = bswap<EL>((uint32_t)native_stat.st_dev);
      host_stat->st_ino = bswap<EL>((uint16_t)native_stat.st_ino);
      host_stat->st_mode = bswap<EL>((uint32_t)native_stat.st_mode);
      host_stat->st_nlink = bswap<EL>((uint16_t)native_stat.st_nlink);
      host_stat->st_uid = bswap<EL>((uint16_t)native_stat.st_uid);
      host_stat->st_gid = bswap<EL>((uint16_t)native_stat.st_gid);
      host_stat->st_size = bswap<EL>((uint32_t)native_stat.st_size);
      host_stat->_st_atime = bswap<EL>((uint32_t)native_stat.st_atime);
      host_stat->_st_mtime = 0;
      host_stat->_st_ctime = 0;
      host_stat->st_blksize = bswap<EL>((uint32_t)native_stat.st_blksize);
      host_stat->st_blocks = bswap<EL>((uint32_t)native_stat.st_blocks);
      break;
    }
    case 33: {
      struct timeval tp;      
      timeval32_t tp32;      
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a0);
      if(globals::enClockFuncts) {
	gettimeofday(&tp, nullptr);
	tp32.tv_sec = bswap<EL>((uint32_t)tp.tv_sec);
	tp32.tv_usec = bswap<EL>((uint32_t)tp.tv_usec);
      }
      else {
	uint64_t mips = globals::icountMIPS*1000000;
	tp.tv_sec = s->icnt / mips;
	tp.tv_usec = ((s->icnt % mips) * 1000000)/ mips;
      }
      tp32.tv_sec = bswap<EL>((uint32_t)tp.tv_sec);
      tp32.tv_usec = bswap<EL>((uint32_t)tp.tv_usec);      
      *reinterpret_cast<timeval32_t*>(s->mem + uptr) = tp32;
      s->gpr[R_v0] = 0;
      break;
    }
    case 34: {
      struct tms tms_buf;
      tms32_t tms32_buf;      
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a0);
      if(globals::enClockFuncts) {
	*((uint32_t*)(&s->gpr[R_v0])) = times(&tms_buf);
      }
      else {
	/* linux returns 100 ticks per second */
	uint64_t mips = globals::icountMIPS*1000000;	
	tms_buf.tms_utime = (s->icnt/mips)*100;
	tms_buf.tms_stime = 0;
	tms_buf.tms_cutime = 0;
	tms_buf.tms_cstime = 0;	
      }
      tms32_buf.tms_utime = bswap<EL>((uint32_t)tms_buf.tms_utime);
      tms32_buf.tms_stime = bswap<EL>((uint32_t)tms_buf.tms_stime);
      tms32_buf.tms_cutime = bswap<EL>((uint32_t)tms_buf.tms_cutime);
      tms32_buf.tms_cstime = bswap<EL>((uint32_t)tms_buf.tms_cstime);      
      *((tms32_t*)(s->mem + uptr)) = tms32_buf;
      break;
    }
    case 35:
      /* int getargs(char **argv) */
      for(int i = 0; i < std::min(MARGS, globals::sArgc); i++) {
	uint32_t arrayAddr = ((uint32_t)s->gpr[R_a0])+4*i;
	uint32_t ptr = bswap<EL>(*((uint32_t*)(s->mem + arrayAddr)));
	strcpy((char*)(s->mem + ptr), globals::sArgv[i]);
      }
      s->gpr[R_v0] = globals::sArgc;
      break;
    case 37: {
      /*char *getcwd(char *buf, uint32_t size) */
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a0);
      char *buf = (char*)(s->mem + uptr);
      assert(getcwd(buf, (uint32_t)s->gpr[R_a1])!=nullptr);
      s->gpr[R_v0] = s->gpr[R_a0];
      break;
    }
    case 38: {
      /* int chdir(const char *path); */
      uint32_t uptr = *reinterpret_cast<uint32_t*>(s->gpr + R_a0);
      const char *path = (char*)(s->mem + uptr);
      s->gpr[R_v0] = chdir(path);
      break;
    }
#if 1
    case 40: {
      fflush(nullptr);
      std::cout << "disassembling " << s->gpr[R_a1] << " insns\n";
      for(int i = 0; i < s->gpr[R_a1]; i++) {
	uint32_t addr =s->gpr[R_a0]+4*i;
	uint8_t *ptr = s->mem + addr;
	uint32_t inst = bswap<EL>(*(uint32_t*)ptr);
	std::cout << "\t"
		  << std::hex << addr << ":" << std::dec
		  << getAsmString(inst,addr)
		  << "\n";
      }
      std::cout << "\n\n";
      break;
    }
#endif
      /* read cycle counter */
    case 50:
      s->gpr[R_v0] = 0;
      break;
      /* flush all caches */
    case 51:
      /* flush address */
    case 52:
      break;
      /* return instruction count */
    case 53:
      s->gpr[R_v0] = s->icnt;
      break;
    case 55: 
      *reinterpret_cast<uint32_t*>(s->mem + (uint32_t)s->gpr[R_a0] + 0) = bswap<EL>(K1SIZE);
      *reinterpret_cast<uint32_t*>(s->mem + (uint32_t)s->gpr[R_a0] + 4) = 0;
      *reinterpret_cast<uint32_t*>(s->mem + (uint32_t)s->gpr[R_a0] + 8) = 0;      
      break;
    default:
      printf("unhandled monitor instruction (reason = %d)\n", reason);
      exit(-1);
      break;
    }
  s->pc = s->gpr[31];
}

template <bool EL>
void _monitor(uint32_t inst, state_t *s) {
  globals::cBB->setTermAddr(s->pc);
  _monitorBody<EL>(inst, s);
  getNextBlock(s);
}

#endif
