#ifndef __MIPSHH__
#define __MIPSHH__

#include <map>
#include <cassert>
#include <string>

static const uint32_t RSVD_INSTRUCTION = 0x00000005;
static const uint32_t RSVD_INSTRUCTION_MASK = 0xFC00003F;
static const uint32_t RSVD_INSTRUCTION_ARG_SHIFT = 6;
static const uint32_t RSVD_INSTRUCTION_ARG_MASK = 0xFFFFF;
static const uint32_t IDT_MONITOR_BASE = 0xBFC00000;
static const uint32_t IDT_MONITOR_SIZE = 2048;
static const int MARGS = 20;

enum {FMT_S = 16, FMT_D, FMT_E, FMT_Q, FMT_W, FMT_L};

enum {COND_F = 0, COND_UN, COND_EQ, COND_UEQ,
      COND_OLT, COND_ULT, COND_OLE, COND_ULE,
      COND_SF, COND_NGLE, COND_SEQ, COND_NGL,
      COND_LT, COND_NGE, COND_LE, COND_NGT};

enum {CP1_CR0 = 0,CP1_CR31,CP1_CR25,CP1_CR26,CP1_CR28};

enum MipsReg_t {
  R_zero=0,R_at,R_v0,R_v1,
  R_a0,R_a1,R_a2,R_a3,
  R_t0,R_t1,R_t2,R_t3,
  R_t4,R_t5,R_t6,R_t7,
  R_s0,R_s1,R_s2,R_s3,
  R_s4,R_s5,R_s6,R_s7,
  R_t8,R_t9,R_k0,R_k1,
  R_gp,R_sp,R_s8,R_ra
};

#define __operation_item(m) m,

#define __fp_operation_list(m)						\
  m(abs)								\
  m(neg)								\
  m(mov)								\
  m(add)								\
  m(sub)								\
  m(mul)								\
  m(div)								\
  m(sqrt)								\
  m(rsqrt)								\
  m(recip)


enum class fpOperation {__fp_operation_list(__operation_item)};

/* doesn't work on freebsd */
#ifdef __linux__
#define __fp_operation_pair(m) {fpOperation::m, #m},
const static std::map<fpOperation, std::string> fpNameMap = {
  __fp_operation_list(__fp_operation_pair)
};
#endif

#define __rtype_operation_list(m)			\
  m(_sll)						\
  m(_movci)						\
  m(_srl)						\
  m(_sra)						\
  m(_sllv)						\
  m(_srlv)						\
  m(_srav)						\
  m(_mfhi)						\
  m(_mthi)						\
  m(_mflo)						\
  m(_mtlo)						\
  m(_mult)						\
  m(_multu)						\
  m(_div)						\
  m(_divu)						\
  m(_add)						\
  m(_addu)						\
  m(_sub)						\
  m(_subu)						\
  m(_and)						\
  m(_or)						\
  m(_xor)						\
  m(_nor)						\
  m(_slt)						\
  m(_sltu)						\
  m(_movn)						\
  m(_movz)						\
  m(_sync)						\
  m(_syscall)						\
  m(_break)						\
  m(_teq)						\
  m(_jalr)						\
  m(_jr)

enum class rtypeOperation {__rtype_operation_list(__operation_item)};

#define __itype_operation_list(m)\
  m(_addi)			 \
  m(_addiu)			 \
  m(_andi)			 \
  m(_ori)			 \
  m(_xori)			 \
  m(_lui)			 \
  m(_slti)			 \
  m(_sltiu)			 \
  m(_lb)			 \
  m(_lh)			 \
  m(_lw)			 \
  m(_sb)			 \
  m(_sh)			 \
  m(_sw)

enum class itypeOperation {__itype_operation_list(__operation_item)};

#define __branch_operation_list(m) \
  m(_beq)			   \
  m(_bne)			   \
  m(_bgtz)			   \
  m(_blez)			   \
  m(_bgez)			   \
  m(_bltz)

enum class branchOperation {__branch_operation_list(__operation_item)};

#define __bl_item(m) m##l,
enum class branchLikelyOperation {__branch_operation_list(__bl_item)};
#undef __bl_item


#define __jump_operation_list(m)		\
  m(_j)						\
  m(_jal)

enum class jumpOperation {__jump_operation_list(__operation_item)};

#define __mips_insn_type_list(m)		\
  m(rtype)					\
  m(jtype)					\
  m(cp0)					\
  m(cp1)					\
  m(cp1x)					\
  m(cp2)					\
  m(special2)					\
  m(special3)					\
  m(ll)						\
  m(sc)						\
  m(itype)

enum class mips_type {__mips_insn_type_list(__operation_item)};
#undef __operation_item


struct rtype_t {
  uint32_t opcode : 6;
  uint32_t sa : 5;
  uint32_t rd : 5;
  uint32_t rt : 5;
  uint32_t rs : 5;
  uint32_t special : 6;
};

struct itype_t {
  uint32_t imm : 16;
  uint32_t rt : 5;
  uint32_t rs : 5;
  uint32_t opcode : 6;
};

struct coproc1x_t {
  uint32_t fmt : 3;
  uint32_t id : 3;
  uint32_t fd : 5;
  uint32_t fs : 5;
  uint32_t ft : 5;
  uint32_t fr : 5;
  uint32_t opcode : 6;
};

struct coproc1_t {
  uint32_t special : 6;
  uint32_t fd : 5;
  uint32_t fs : 5;
  uint32_t ft : 5;
  uint32_t fmt : 5;
  uint32_t opcode : 6;
};

struct lwxc1_t {
  uint32_t id : 6;
  uint32_t fd : 5;
  uint32_t pad : 5;
  uint32_t index : 5;
  uint32_t base : 5;
  uint32_t opcode : 6;
};

union mips_t {
  rtype_t r;
  itype_t i;
  coproc1_t f;
  coproc1x_t c1x;
  lwxc1_t lc1x;
  uint32_t raw;
  mips_t(uint32_t x) : raw(x) {}
};


static inline mips_type getInsnType(uint32_t insn) {
  uint32_t opcode = insn>>26;
  if(opcode==0)
    return mips_type::rtype;
  else if(((opcode>>1)==1))
    return mips_type::jtype;
  else if(opcode == 0x10)
    return mips_type::cp0;
  else if(opcode == 0x11)
    return mips_type::cp1;
  else if(opcode == 0x12)
    return mips_type::cp2;
  else if(opcode == 0x13)
    return mips_type::cp1x;
  else if(opcode == 0x1c)
    return mips_type::special2;
  else if(opcode == 0x1f)
    return mips_type::special3;
  else if(opcode == 0x30)
    return mips_type::ll;
  else if(opcode == 0x38)
    return mips_type::sc;
  else
    return mips_type::itype;
}

static inline bool isFloatingPoint(uint32_t inst) {
  return ((inst>>26) == 0x11);
}

inline bool is_monitor(uint32_t inst) {
  uint32_t opcode = inst>>26;
  return (opcode==0) && ((inst&63) == 0x5);
}

static inline bool is_jr(uint32_t inst) {
  uint32_t opcode = inst>>26;
  uint32_t funct = inst & 63;
  return (opcode==0) and (funct == 0x08);
}

static inline bool is_jal(uint32_t inst) {
  uint32_t opcode = inst>>26;
  return (opcode == 3);
}

inline bool is_jalr(uint32_t inst) {
  uint32_t opcode = inst>>26;
  return (opcode==0) && ((inst&63) == 0x9);
}


static inline bool is_j(uint32_t inst) {
  uint32_t opcode = inst>>26;
  return (opcode == 2);
}

static inline uint32_t get_jump_target(uint32_t pc, uint32_t inst) {
  assert(is_jal(inst) or is_j(inst));
  static const uint32_t pc_mask = (~((1U<<28)-1));
  uint32_t jaddr = (inst & ((1<<26)-1)) << 2;
  return ((pc + 4)&pc_mask) | jaddr;
}

static inline bool is_branch(uint32_t inst) {
  uint32_t opcode = inst>>26;
  switch(opcode)
    {
    case 0x01:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
      return true;
    case 0x11:
      return (((inst >> 21) & 31) == 0x8);
      break;
    case 0x14:
    case 0x16:
    case 0x15:
    case 0x17:
      return true;
    default:
      break;
    }
  return false;
}

static inline uint32_t get_branch_target(uint32_t pc, uint32_t inst) {
  int16_t himm = (int16_t)(inst & ((1<<16) - 1));
  int32_t imm = ((int32_t)himm) << 2;
  return  pc+4+imm; 
}

static inline bool isDirectBranchOrJump(uint32_t insn, uint32_t addr, uint32_t &target) {
 uint32_t opcode = insn >> 26;
  switch(opcode)
    {
      /* jump */
    case 0x02: {
      uint32_t jaddr = (insn & ((1<<26)-1)) << 2;
      jaddr |= ((addr+4) & (~((1<<28)-1)));
      target = jaddr;
      return true;
    }
      /* branches */
    case 0x01:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: {
      int16_t himm = (int16_t)(insn & ((1<<16) - 1));
      int32_t imm = ((int32_t)himm) << 2;
      target = imm + addr + 4;
      return true;
    }
      /* coproc 1*/
    case 0x11: {
      if(((insn >> 21) & 31) == 0x8) {
	int16_t himm = (int16_t)(insn & ((1<<16) - 1));
	int32_t imm = ((int32_t)himm) << 2;
	target = imm + addr + 4;
	return true;
      }
      else {
	return false;
      }
      break;
    }
    default:
      target = ~0U;
      break;
    }
  return false;
}

static inline bool isBranchOrJump(uint32_t inst) {
  switch(getInsnType(inst))
    {
    case mips_type::rtype:
      if( ((inst&63) == 0x08) || ((inst&63) == 0x09)) {
	return true;
      }
      return false;
    case mips_type::itype:
      return is_branch(inst);
    case mips_type::jtype:
      return true;
    case mips_type::cp1:
      return (((inst >> 21) & 31) == 0x8);
    default:
      break;
    }
  return false;
}

static inline int32_t signExtendImm(itype_t i) {
  return static_cast<int32_t>(static_cast<int16_t>(i.imm));
}

#endif
