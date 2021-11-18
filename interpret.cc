#include "interpret.hh"
#include <cassert>         // for assert
#include <cmath>           // for isnan, sqrt
#include <cstdio>          // for printf
#include <cstdlib>         // for exit, abs
#include <iostream>        // for operator<<, basic_ostream<>::__ostream_type
#include <limits>          // for numeric_limits
#include <string>          // for string
#include <type_traits>     // for enable_if, is_integral
#include "basicBlock.hh"   // for basicBlock
#include "disassemble.hh"  // for getCondName
#include "helper.hh"       // for extractBit, UNREACHABLE, bswap, setBit
#include "mips.hh"         // for mips_t, rtypeOperation, itype_t, rtype_t
#include "saveState.hh"    // for dumpState
#include "state.hh"        // for state_t, operator<<
#define ELIDE_LLVM
#include "globals.hh"      // for cBB, blobName, isMipsEL
#include "monitor.hh"      // for _monitor, getNextBlock

template <bool appendIns, bool EL> void execMips(state_t *s);
template<bool EL> void _lwl(uint32_t inst, state_t *s);
template<bool EL> void _lwr(uint32_t inst, state_t *s);
template<bool EL> void _swl(uint32_t inst, state_t *s);
template<bool EL> void _swr(uint32_t inst, state_t *s);
template<bool EL> void _sc(uint32_t inst, state_t *s);

static void _c(uint32_t inst, state_t *s);
static void _truncw(uint32_t inst, state_t *s);
static void _fmovc(uint32_t inst, state_t *s);

template<bool fmovz> static void _fmov(uint32_t inst, state_t *s);

static void getNextBlock(state_t *s) {
  basicBlock *nBB = globals::cBB->findBlock(s->pc);
  if(nBB == nullptr ) {
    nBB = new basicBlock(s->pc, globals::cBB);
  }
  globals::cBB->setReadOnly();
  globals::cBB = nBB;
}

template<typename T, bool isLoad, bool EL>
void fpMemOp(uint32_t inst, state_t *s) {
  uint32_t ft=(inst>>16)&31,rs=(inst>>21)&31;
  int32_t imm = static_cast<int32_t>(static_cast<int16_t>(inst & ((1<<16) - 1)));
  uint32_t ea = s->gpr[rs] + imm;
  if(isLoad)
    *reinterpret_cast<T*>(s->cpr1 + ft) = bswap<EL>(*reinterpret_cast<T*>(s->mem + ea));
  else
    *reinterpret_cast<T*>(s->mem + ea) = bswap<EL>(*reinterpret_cast<T*>(s->cpr1 + ft));
  s->pc += 4;
}

template< typename T, fpOperation op>
void execFP(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  T _fs = *reinterpret_cast<T*>(s->cpr1+mi.f.fs);
  T _ft = *reinterpret_cast<T*>(s->cpr1+mi.f.ft);
  T &_fd = *reinterpret_cast<T*>(s->cpr1+mi.f.fd);

  switch(op)
    {
    case fpOperation::abs:
      _fd = std::abs(_fs);
      break;
    case fpOperation::neg:
      _fd = -_fs;
      break;
    case fpOperation::mov:
      _fd = _fs;
      break;
    case fpOperation::add:
      _fd = _fs + _ft;
      break;
    case fpOperation::sub:
      _fd = _fs - _ft;
      break;
    case fpOperation::mul:
      _fd = _fs * _ft;
      break;
    case fpOperation::div:
      _fd = _ft==0.0 ? std::numeric_limits<T>::max() : (_fs / _ft);
      break;
    case fpOperation::sqrt:
      _fd = std::sqrt(_fs);
      break;
    case fpOperation::rsqrt:
      _fd = static_cast<T>(1.0) / std::sqrt(_fs);
      break;
    case fpOperation::recip:
      _fd = static_cast<T>(1.0) / _fs;
      break;
    default:
      UNREACHABLE();
    }
  s->pc+=4;
}

template <fpOperation op>
void do_fp_op(uint32_t inst, state_t *s) {
  switch((inst>>21)&31) {
  case FMT_S:
    execFP<float,op>(inst,s);
    break;
  case FMT_D:
    execFP<double,op>(inst,s);
    break;
  default:
    UNREACHABLE();
  }
}


template <bool appendIns, bool EL>
void _bgez_bltz(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  int32_t npc = s->pc+4;
  bool takeBranch = false;  
  if(appendIns) globals::cBB->setTermAddr(s->pc);
  if(mi.i.rt==0 || mi.i.rt==1) {
    takeBranch = mi.i.rt==0 ? (s->gpr[mi.i.rs] < 0) : (s->gpr[mi.i.rs] >= 0);
    s->pc += 4;
    execMips<appendIns,EL>(s);
    s->pc = takeBranch ? (signExtendImm(mi.i)<<2)+npc : s->pc;
  }
  else if(mi.i.rt==2 || mi.i.rt==3) {
    if(appendIns) globals::cBB->setBranchLikely();
    takeBranch = mi.i.rt==2 ? (s->gpr[mi.i.rs] < 0) : (s->gpr[mi.i.rs] >= 0);
    s->pc += 4;
    if(takeBranch) {
      execMips<appendIns,EL>(s);
      s->pc = (signExtendImm(mi.i)<<2)+npc;
    }
    else {
      if(appendIns) {
	uint32_t bInst = bswap<EL>(*(uint32_t*)(s->mem + s->pc));
	globals::cBB->addIns(bInst, s->pc);
      }
      s->pc += 4;
    }
  }
  getNextBlock(s);
}

template<branchOperation op, bool appendIns, bool EL>
void branch(uint32_t inst, state_t *s) {
  mips_t mi(inst);  
  if(appendIns) {
    globals::cBB->setTermAddr(s->pc);
  }
  int32_t npc = s->pc+4; 
  bool takeBranch = false;
  switch(op)
    {
    case branchOperation::_beq:
      takeBranch = (s->gpr[mi.i.rt] == s->gpr[mi.i.rs]);
      break;
    case branchOperation::_bne:
      takeBranch = (s->gpr[mi.i.rt] != s->gpr[mi.i.rs]);
      break;
    case branchOperation::_bgtz:
      takeBranch = (s->gpr[mi.i.rs]>0);
      break;
    case branchOperation::_blez:
      takeBranch = (s->gpr[mi.i.rs]<=0);
      break;
    default:
      UNREACHABLE();
    }
  s->pc += 4;
  execMips<appendIns,EL>(s);
  if(takeBranch)
    s->pc = (signExtendImm(mi.i)<<2)+npc;
  getNextBlock(s);
}

template<branchLikelyOperation op, bool appendIns, bool EL>
void branchLikely(uint32_t inst, state_t *s) {
  mips_t mi(inst);    
  int32_t npc = s->pc+4; 
  bool takeBranch = false;
  if(appendIns) {
    globals::cBB->setTermAddr(s->pc);
    globals::cBB->setBranchLikely();
  }
  s->pc +=4;
  switch(op)
    {
    case branchLikelyOperation::_beql:
      takeBranch = (s->gpr[mi.i.rt] == s->gpr[mi.i.rs]);
      break;
    case branchLikelyOperation::_bnel:
      takeBranch = (s->gpr[mi.i.rt] != s->gpr[mi.i.rs]);
      break;
    case branchLikelyOperation::_bgtzl:
      takeBranch = (s->gpr[mi.i.rs]>0);
      break;
    case branchLikelyOperation::_blezl:
      takeBranch = (s->gpr[mi.i.rs]<=0);
      break;
    default:
      UNREACHABLE();
    }
  if(takeBranch) {
    execMips<appendIns,EL>(s);
    s->pc = ((signExtendImm(mi.i)<<2)+npc);
  }
  else {
    if(appendIns) {
      uint32_t bInst = bswap<EL>(*reinterpret_cast<uint32_t*>(s->mem + s->pc));
      globals::cBB->addIns(bInst, s->pc);
    }
    s->pc += 4;
  }
  getNextBlock(s);
}


template <bool EL, typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
void itype_loadu_helper(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  uint32_t ea = s->gpr[mi.i.rs] + signExtendImm(mi.i);
  *reinterpret_cast<uint32_t*>(s->gpr + mi.i.rt) = bswap<EL>(*reinterpret_cast<T*>(s->mem + ea));
  s->pc += 4;
}


template<bool EL, typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
void itype_load_helper(mips_t mi, state_t *s) {
  uint32_t ea = static_cast<uint32_t>(s->gpr[mi.i.rs]) + signExtendImm(mi.i);
  T mem = bswap<EL>(*(reinterpret_cast<T*>(s->mem + ea)));
  s->gpr[mi.i.rt] = static_cast<int32_t>(mem);
}

template<bool EL, typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
void itype_store_helper(mips_t mi, state_t *s) {
  uint32_t ea = static_cast<uint32_t>(s->gpr[mi.i.rs]) + signExtendImm(mi.i);
  *reinterpret_cast<T*>(s->mem+ea) = bswap<EL,T>(static_cast<T>(s->gpr[mi.i.rt]));
}

template<itypeOperation op, bool EL>
void exec_itype(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  bool bump_pc = true;
  uint32_t uimm32 = inst & ((1<<16) - 1);
  int16_t imm16 = static_cast<int16_t>(uimm32);
  int32_t imm32 = static_cast<int32_t>(imm16);
    
  switch(op)
    {
    case itypeOperation::_addi:
      s->gpr[mi.i.rt] = s->gpr[mi.i.rs] + imm32;
      break;
    case itypeOperation::_addiu:
      /* is this correct? */
      s->gpr[mi.i.rt] = s->gpr[mi.i.rs] + imm32;  
      break;
    case itypeOperation::_andi:
      s->gpr[mi.i.rt] = s->gpr[mi.i.rs] & uimm32;  
      break;
    case itypeOperation::_ori:
      s->gpr[mi.i.rt] = s->gpr[mi.i.rs] | uimm32;  
      break;
    case itypeOperation::_xori:
      s->gpr[mi.i.rt] = s->gpr[mi.i.rs] ^ uimm32;  
      break;
    case itypeOperation::_lui:
      s->gpr[mi.r.rt] = uimm32 << 16;
      break;
    case itypeOperation::_slti:
      s->gpr[mi.i.rt] = s->gpr[mi.i.rs] < imm32;  
      break;
    case itypeOperation::_sltiu:
      s->gpr[mi.i.rt] = static_cast<uint32_t>(s->gpr[mi.i.rs]) < static_cast<uint32_t>(imm32);
      break;
    case itypeOperation::_lb:
      itype_load_helper<EL,int8_t>(mi, s);
      break;
    case itypeOperation::_lh:
      itype_load_helper<EL,int16_t>(mi, s);
      break;
    case itypeOperation::_lw:
      itype_load_helper<EL,int32_t>(mi, s);
      break;
    case itypeOperation::_sw:
      itype_store_helper<EL,int32_t>(mi, s);
      break;
    case itypeOperation::_sh:
      itype_store_helper<EL, int16_t>(mi, s);
      break;
    case itypeOperation::_sb:
      itype_store_helper<EL,int8_t>(mi, s);
      break;
    default:
      UNREACHABLE();
    }
  if(bump_pc) {
    s->pc += 4;
  }
}

template<rtypeOperation op, bool appendIns, bool EL>
void exec_rtype(uint32_t inst, state_t* s) {
  mips_t mi(inst);
  int32_t rs = s->gpr[mi.r.rs], rt = s->gpr[mi.r.rt];
  uint32_t u_rs = static_cast<uint32_t>(s->gpr[mi.r.rs]);
  uint32_t u_rt = static_cast<uint32_t>(s->gpr[mi.r.rt]);
  bool bump_pc = true;
  switch(op)
    {
    case rtypeOperation::_sll:
      s->gpr[mi.r.rd] = s->gpr[mi.r.rt] << mi.r.sa;
      break;
    case rtypeOperation::_movci: {
      uint32_t cc = (inst >> 18) & 7;
      uint32_t tf = (inst>>16) & 1;
      if(extractBit(s->fcr1[CP1_CR25], cc)==tf) {
	s->gpr[mi.r.rd] = rs;
      }
      break;
    }
    case rtypeOperation::_srl:
      s->gpr[mi.r.rd] = u_rt >> mi.r.sa;
      break;
    case rtypeOperation::_sra:
      s->gpr[mi.r.rd] = rt >> mi.r.sa;
      break;
    case rtypeOperation::_sllv:
      s->gpr[mi.r.rd] = rt << (rs & 0x1f);
      break;
    case rtypeOperation::_srlv:
      s->gpr[mi.r.rd] = u_rt >> (rs & 0x1f);
      break;
    case rtypeOperation::_srav:
      s->gpr[mi.r.rd] = rt >> (rs & 0x1f);
      break;
    case rtypeOperation::_mfhi:
      s->gpr[mi.r.rd] = s->hi;
      break;
    case rtypeOperation::_mthi:
      s->hi = rs;
      break;
    case rtypeOperation::_mflo:
      s->gpr[mi.r.rd] = s->lo;
      break;
    case rtypeOperation::_mtlo:
      s->lo = rs;
      break;
    case rtypeOperation::_mult:	{
      int64_t y = static_cast<int64_t>(rs) * static_cast<int64_t>(rt);
      s->lo = static_cast<int32_t>(y & 0xffffffff);
      s->hi = static_cast<int32_t>(y >> 32);
      break;
    }
    case rtypeOperation::_multu: {
      uint64_t u_rs = static_cast<uint64_t>(*reinterpret_cast<uint32_t*>(&rs));
      uint64_t u_rt = static_cast<uint64_t>(*reinterpret_cast<uint32_t*>(&rt));
      uint64_t y = u_rs * u_rt;

      *reinterpret_cast<uint32_t*>(&(s->lo)) = static_cast<uint32_t>(y);
      *reinterpret_cast<uint32_t*>(&(s->hi)) = static_cast<uint32_t>(y>>32);
      break;
    }
    case rtypeOperation::_div:						
      if(rt != 0) {
	s->lo = rs / rt;
	s->hi = rs % rt;
      }
      break;
    case rtypeOperation::_divu:
      if(u_rt != 0) {
	s->lo = u_rs / u_rt;
	s->hi = u_rs % u_rt;
      }
      break;
    case rtypeOperation::_add:
      s->gpr[mi.r.rd] = rs + rt;
      break;
    case rtypeOperation::_addu:
      s->gpr[mi.r.rd] = u_rs + u_rt;
      break;
    case rtypeOperation::_sub:
      UNREACHABLE();
    case rtypeOperation::_subu:
      s->gpr[mi.r.rd] = u_rs - u_rt;
      break;
    case rtypeOperation::_and:
      s->gpr[mi.r.rd] = rs & rt;
      break;
    case rtypeOperation::_or:
      s->gpr[mi.r.rd] = rs | rt;
      break;
    case rtypeOperation::_xor:
      s->gpr[mi.r.rd] = rs ^ rt;
      break;
    case rtypeOperation::_nor:
      s->gpr[mi.r.rd] = ~(rs | rt);
      break;
    case rtypeOperation::_slt:		
      s->gpr[mi.r.rd] = rs < rt;
      break;
    case rtypeOperation::_sltu:
      s->gpr[mi.r.rd] = u_rs < u_rt;
      break;
    case rtypeOperation::_movn: 
      if(rt != 0)
	s->gpr[mi.r.rd] = rs;
      break;
    case rtypeOperation::_movz:
      if(rt == 0)
	s->gpr[mi.r.rd] = rs;
      break;
    case rtypeOperation::_sync:
      basicBlock::dropAllBBs();
      globals::cBB = new basicBlock(s->pc+4);
      break;
    case rtypeOperation::_syscall: {
      s->pc += 4;
      std::cout << "got syscall, saving machine state\n";
      dumpState(*s, globals::blobName);
      s->brk = 1;
      bump_pc = false;
      break;
    }
    case rtypeOperation::_break:
      s->brk = 1;
      bump_pc = false;
      break;
    case rtypeOperation::_teq:
      if(rs == rt) {
	std::cerr << "TEQ TRAP : 0x"
		  << std::hex << s->pc << std::dec
		  << "\n";
	s->brk=1;
	bump_pc = false;
      }
      break;
    case rtypeOperation::_jalr: {
      uint32_t jaddr = s->gpr[mi.r.rs];
      s->gpr[31] = s->pc+8;
      globals::cBB->setTermAddr(s->pc);
      s->pc += 4;
      execMips<appendIns,EL>(s);
      s->pc = jaddr;
      getNextBlock(s);
      bump_pc = false;
      break;
    }
    case rtypeOperation::_jr: {
      uint32_t jaddr = s->gpr[mi.r.rs];
      globals::cBB->setTermAddr(s->pc);
      s->pc += 4;
      execMips<appendIns,EL>(s);
      s->pc = jaddr;
      getNextBlock(s);
      bump_pc = false;
      break;
    }
    default:
      UNREACHABLE();
    }
  
  if(bump_pc) { 
    s->pc+=4;
  }
}

template <bool appendIns>
void execSpecial2(uint32_t inst,state_t *s) {
  uint32_t funct = inst & 63;
  mips_t mi(inst);
  int64_t y,acc;  
  switch(funct)
    {
    case(0x0): { //madd 
      acc = ((int64_t)s->hi) << 32;
      acc |= ((int64_t)s->lo);
      y = (int64_t)s->gpr[mi.r.rs] * (int64_t)s->gpr[mi.r.rt];
      y += acc;
      s->lo = (int32_t)(y & 0xffffffff);
      s->hi = (int32_t)(y >> 32);
    }
      break;
    case 0x1: { //maddu
        uint64_t y,acc;
	uint64_t u0 = (uint64_t)*((uint32_t*)&s->gpr[mi.r.rs]);
	uint64_t u1 = (uint64_t)*((uint32_t*)&s->gpr[mi.r.rt]);
	y = u0*u1;
	acc = ((uint64_t)s->hi) << 32;
	acc |= ((uint64_t)s->lo);
	y += acc;
	s->lo = (uint32_t)(y & 0xffffffff);
	s->hi = (uint32_t)(y >> 32);
    }
      break;
    case(0x2): { //mul
      int64_t y = ((int64_t)s->gpr[mi.r.rs]) * ((int64_t)s->gpr[mi.r.rt]);
      s->gpr[mi.r.rd] = (int32_t)y;
    }
      break;
    case 0x4: {//msub
      acc = ((int64_t)s->hi) << 32;
      acc |= ((int64_t)s->lo);
      y = (int64_t)s->gpr[mi.r.rs] * (int64_t)s->gpr[mi.r.rt];
      y = acc - y;
      s->lo = (int32_t)(y & 0xffffffff);
      s->hi = (int32_t)(y >> 32);
    }
      break;
    case(0x20): //clz
      s->gpr[mi.r.rd] = (s->gpr[mi.r.rs]==0) ? 32 : __builtin_clz(s->gpr[mi.r.rs]);
      break;
    default:
      UNREACHABLE();
    }
  s->pc += 4;
}

template <bool appendIns>
void execSpecial3(uint32_t inst,state_t *s) {
  uint32_t funct = inst & 63;
  uint32_t op = (inst>>6) & 31;
  mips_t mi(inst);
  if(funct == 32) {
    switch(op)
      {
      case 0x10: { /* seb */
	s->gpr[mi.r.rd] = (int32_t)((int8_t)s->gpr[mi.r.rt]);	
	s->pc +=4;
	break;
      }
	break;
      case 0x18: { /* seh */
	s->gpr[mi.r.rd] = static_cast<int32_t>(static_cast<int16_t>(s->gpr[mi.r.rt]));	
	s->pc +=4;
	break;
      }

      default:
	UNREACHABLE();
      }
  }
  else if(funct == 0) { /* ext */
    uint32_t pos = (inst >> 6) & 31;
    uint32_t size = ((inst >> 11) & 31) + 1;
    s->gpr[mi.r.rt] = (s->gpr[mi.r.rs] >> pos) & ((1<<size)-1);
    s->pc += 4;
  }
  else if(funct == 0x4) { /* ins */
    uint32_t lsb = (inst >> 6) & 31;
    uint32_t msb = ((inst >> 11) & 31);
    uint32_t size = msb-lsb+1;
    uint32_t mask = (1U<<size) -1;
    uint32_t cmask = ~(mask << lsb);
    uint32_t v = (s->gpr[mi.r.rs] & mask) << lsb;
    uint32_t c = (s->gpr[mi.r.rt] & cmask) | v;
    s->gpr[mi.r.rt] = c;
    s->pc += 4;
  }
  else
    UNREACHABLE();
}


template <bool appendIns,bool EL>
void execRType(uint32_t inst,state_t *s) {
  mips_t mi(inst);
  switch(mi.r.opcode)
    {
    case 0x00:
      exec_rtype<rtypeOperation::_sll,appendIns,EL>(inst, s);
      break;
    case 0x01:
      exec_rtype<rtypeOperation::_movci,appendIns,EL>(inst, s);
      break;
    case 0x02:
      exec_rtype<rtypeOperation::_srl,appendIns,EL>(inst, s);
      break;
    case 0x03:
      exec_rtype<rtypeOperation::_sra,appendIns,EL>(inst,s);
      break;
    case 0x04:
      exec_rtype<rtypeOperation::_sllv,appendIns,EL>(inst,s);
      break;
    case 0x05:
      _monitor<EL>(inst,s);
      break;
    case 0x06:
      exec_rtype<rtypeOperation::_srlv,appendIns,EL>(inst,s);
      break;
    case 0x07:
      exec_rtype<rtypeOperation::_srav,appendIns,EL>(inst,s);
      break;
    case 0x08:
      exec_rtype<rtypeOperation::_jr,appendIns,EL>(inst,s);
      break;
    case 0x09:
      exec_rtype<rtypeOperation::_jalr,appendIns,EL>(inst,s);
      break;
    case 0x0C:
      exec_rtype<rtypeOperation::_syscall,appendIns,EL>(inst,s);
      break;
    case 0x0D:
      exec_rtype<rtypeOperation::_break,appendIns,EL>(inst,s);
      break;
    case 0x0f:
      exec_rtype<rtypeOperation::_sync,appendIns,EL>(inst,s);
      break;
    case 0x10:
      exec_rtype<rtypeOperation::_mfhi,appendIns,EL>(inst,s);
      break;
    case 0x11:
      exec_rtype<rtypeOperation::_mthi,appendIns,EL>(inst,s);
      break;
    case 0x12:
      exec_rtype<rtypeOperation::_mflo,appendIns,EL>(inst,s);
      break;
    case 0x13:
      exec_rtype<rtypeOperation::_mtlo,appendIns,EL>(inst,s);
      break;
    case 0x18:
      exec_rtype<rtypeOperation::_mult,appendIns,EL>(inst,s);
      break;
    case 0x19:
      exec_rtype<rtypeOperation::_multu,appendIns,EL>(inst,s);
      break;
    case 0x1A:
      exec_rtype<rtypeOperation::_div,appendIns,EL>(inst,s);
      break;
    case 0x1B:
      exec_rtype<rtypeOperation::_divu,appendIns,EL>(inst,s);
      break;
    case 0x20:
      exec_rtype<rtypeOperation::_add,appendIns,EL>(inst,s);
      break;
    case 0x21:
      exec_rtype<rtypeOperation::_addu,appendIns,EL>(inst,s);
      break;
    case 0x22:
      exec_rtype<rtypeOperation::_sub,appendIns,EL>(inst,s);
      break;
    case 0x23:
      exec_rtype<rtypeOperation::_subu,appendIns,EL>(inst,s);
      break;
    case 0x24:
      exec_rtype<rtypeOperation::_and,appendIns,EL>(inst,s);
      break;
    case 0x25:
      exec_rtype<rtypeOperation::_or,appendIns,EL>(inst,s);
      break;
    case 0x26:
      exec_rtype<rtypeOperation::_xor,appendIns,EL>(inst,s);
      break;
    case 0x27:
      exec_rtype<rtypeOperation::_nor,appendIns,EL>(inst,s);
      break;
    case 0x2A:
      exec_rtype<rtypeOperation::_slt,appendIns,EL>(inst,s);
      break;
    case 0x2B: 
      exec_rtype<rtypeOperation::_sltu,appendIns,EL>(inst,s);
      break;
    case 0x0B:
      exec_rtype<rtypeOperation::_movn,appendIns,EL>(inst,s);
      break;
    case 0x0A:
      exec_rtype<rtypeOperation::_movz,appendIns,EL>(inst,s);
      break;
    case 0x34:
      exec_rtype<rtypeOperation::_teq,appendIns,EL>(inst,s);
      break;
    default:
      std::cerr << *s << "\n";      
      UNREACHABLE();
    }
}

template <jumpOperation op, bool appendIns, bool EL>
void jump(uint32_t inst, state_t *s) {
  globals::cBB->setTermAddr(s->pc);
  uint32_t jaddr = (inst & ((1<<26)-1)) << 2;
  if(op == jumpOperation::_jal) {
    s->gpr[R_ra] = s->pc+8;
  }
  s->pc += 4;
  jaddr |= (s->pc & (~((1<<28)-1)));
  execMips<appendIns,EL>(s);
  s->pc = jaddr;
  getNextBlock(s);
}

template <bool appendIns, bool EL>
void execJType(uint32_t inst, state_t *s) {
  uint32_t opcode = inst>>26;
  if(opcode==0x2)
    jump<jumpOperation::_j,appendIns,EL>(inst,s);
  else if(opcode==0x3)
    jump<jumpOperation::_jal,appendIns,EL>(inst, s);
  else
    UNREACHABLE();
}


template <bool appendIns, bool EL>
void execIType(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  switch(mi.i.opcode)
    {
    case 0x01:
      _bgez_bltz<appendIns,EL>(inst,s);
      break;
    case 0x04:
      branch<branchOperation::_beq,appendIns,EL>(inst, s);
      break;
    case 0x05:
      branch<branchOperation::_bne,appendIns,EL>(inst, s);
      break;
    case 0x06:
      branch<branchOperation::_blez,appendIns,EL>(inst, s);
      break;
    case 0x07:
      branch<branchOperation::_bgtz,appendIns,EL>(inst, s);
      break;
    case 0x08:
      exec_itype<itypeOperation::_addi,EL>(inst,s);
      break;
    case 0x09:
      exec_itype<itypeOperation::_addiu,EL>(inst,s);
      break;
    case 0x0a:
      exec_itype<itypeOperation::_slti,EL>(inst,s);
      break;
    case 0x0b:
      exec_itype<itypeOperation::_sltiu,EL>(inst,s);
      break;
    case 0x0c:
      exec_itype<itypeOperation::_andi,EL>(inst,s);
      break;
    case 0x0d:
      exec_itype<itypeOperation::_ori,EL>(inst,s);
      break;
    case 0x0e:
      exec_itype<itypeOperation::_xori,EL>(inst,s);
      break;
    case 0x0f:
      exec_itype<itypeOperation::_lui,EL>(inst,s);
      break;
    case 0x14:
      branchLikely<branchLikelyOperation::_beql,appendIns,EL>(inst, s);
      break;
    case 0x15:
      branchLikely<branchLikelyOperation::_bnel,appendIns,EL>(inst, s);
      break;
    case 0x16:
      branchLikely<branchLikelyOperation::_blezl,appendIns,EL>(inst, s);
      break;
    case 0x17:
      branchLikely<branchLikelyOperation::_bgtzl,appendIns,EL>(inst, s);
      break;
    case 0x20:
      exec_itype<itypeOperation::_lb,EL>(inst,s);
      break;
    case 0x21:
      exec_itype<itypeOperation::_lh,EL>(inst,s);
      break;
    case 0x22:
      _lwl<EL>(inst,s);
      break;
    case 0x23:
      exec_itype<itypeOperation::_lw,EL>(inst,s);
      break;
    case 0x24: 
      itype_loadu_helper<EL,uint8_t>(inst,s);
      break;
    case 0x25:
      itype_loadu_helper<EL,uint16_t>(inst,s);      
      break;
    case 0x26:
      _lwr<EL>(inst,s);
      break;
    case 0x28:
      exec_itype<itypeOperation::_sb,EL>(inst,s);
      break;
    case 0x29:
      exec_itype<itypeOperation::_sh,EL>(inst,s);
      break;
    case 0x2a:
      _swl<EL>(inst,s);
      break;
    case 0x2b:
      exec_itype<itypeOperation::_sw,EL>(inst,s);
      break;
    case 0x2e:
      _swr<EL>(inst,s);
      break;
    case 0x31:
      fpMemOp<int32_t, true, EL>(inst, s);
      break;
    case 0x35:
      fpMemOp<int64_t, true, EL>(inst, s);
      break;
    case 0x39:
      fpMemOp<int32_t, false, EL>(inst, s);
      break;
    case 0x3d:
      fpMemOp<int64_t, false, EL>(inst, s);
      break;
    default:
      UNREACHABLE();
    }
}

template <bool appendIns>
void execCoproc0(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  switch((inst>>21) & 31) {
  case 0x0:
    s->gpr[mi.r.rt] = s->cpr0[mi.r.rd];
    break;
  case 0x4:
    s->cpr0[mi.r.rd] = s->gpr[mi.r.rt];
    break;
  default:
    UNREACHABLE();
  }
  s->pc += 4;
}

template <bool polarity, bool appendIns, bool EL>
void _bc1(uint32_t inst, state_t *s) {
  globals::cBB->setTermAddr(s->pc);
  int16_t himm = (int16_t)(inst & ((1<<16) - 1));
  int32_t npc = (static_cast<int32_t>(himm) << 2) + s->pc+ + 4;
  uint32_t cc = (inst >> 18) & 7;
  bool takeBranch = extractBit(s->fcr1[CP1_CR25], cc)==polarity;
  s->pc += 4;
  execMips<appendIns,EL>(s);
  if(takeBranch)
    s->pc = npc;
  getNextBlock(s);
}

template <bool polarity, bool appendIns, bool EL>
void _bc1l(uint32_t inst, state_t *s) {
  globals::cBB->setTermAddr(s->pc);
  globals::cBB->setBranchLikely();
  int16_t himm = (int16_t)(inst & ((1<<16) - 1));
  int32_t npc = (static_cast<int32_t>(himm) << 2) + s->pc+ + 4;
  uint32_t cc = (inst >> 18) & 7;
  bool takeBranch = extractBit(s->fcr1[CP1_CR25], cc)==polarity;
  s->pc +=4;
  if(takeBranch) {
    execMips<appendIns,EL>(s);
    s->pc = npc;
  }
  else {
    if(appendIns) {
      uint32_t bInst = bswap<EL>(*(uint32_t*)(s->mem + s->pc));
      globals::cBB->addIns(bInst, s->pc);
    }
    s->pc += 4;
  }
  getNextBlock(s);
}

template <bool appendIns, bool EL>
void execCoproc1(uint32_t inst, state_t *s) {
  uint32_t opcode = inst>>26;
  uint32_t functField = (inst>>21) & 31;
  uint32_t lowop = inst & 63;  
  uint32_t fmt = (inst >> 21) & 31;
  uint32_t nd_tf = (inst>>16) & 3;
  
  uint32_t lowbits = inst & ((1<<11)-1);
  opcode &= 0x3;

  if(fmt == 0x8) {
    switch(nd_tf)
      {
      case 0x0:
	_bc1<false,appendIns,EL>(inst,s);
	break;
      case 0x1:
	_bc1<true,appendIns,EL>(inst,s);
	break;
      case 0x2:
	_bc1l<false,appendIns,EL>(inst,s);
	break;
      case 0x3:
	_bc1l<true,appendIns,EL>(inst,s);
	break;
      }
  }
  else if((lowbits == 0) && ((functField==0x0) || (functField==0x4))) {
    mips_t mi(inst);
    if(functField == 0x0)
      s->gpr[mi.r.rt] = s->cpr1[mi.r.rd];
    else if(functField == 0x4)
      s->cpr1[mi.r.rd] = s->gpr[mi.r.rt];
    s->pc += 4;
  }
  else {
    if((lowop >> 4) == 3) {
      _c(inst, s);
    }
    else {
      switch(lowop)
	{
	  case 0x0:
	    do_fp_op<fpOperation::add>(inst, s);
	    break;
	  case 0x1:
	    do_fp_op<fpOperation::sub>(inst, s);
	    break;
	  case 0x2:
	    do_fp_op<fpOperation::mul>(inst, s);
	    break;
	  case 0x3:
	    do_fp_op<fpOperation::div>(inst, s);
	    break;
	  case 0x4:
	    do_fp_op<fpOperation::sqrt>(inst, s);
	    break;
	  case 0x5:
	    do_fp_op<fpOperation::abs>(inst, s);
	    break;
	  case 0x6:
	    do_fp_op<fpOperation::mov>(inst, s);
	    break;
	  case 0x7:
	    do_fp_op<fpOperation::neg>(inst, s);
	    break;
          case 0x9: /* truncl - not needed? */
	    UNREACHABLE();
	  case 0xd:
	    _truncw(inst, s);
	    break;
	  case 0x11:
	    _fmovc(inst, s);
	    break;
	  case 0x12:
	    _fmov<true>(inst, s);
	    break;
	  case 0x13:
	    _fmov<false>(inst, s);
	    break;
	  case 0x15:
	    do_fp_op<fpOperation::recip>(inst, s);
	    break;
	  case 0x16:
	    do_fp_op<fpOperation::rsqrt>(inst, s);
	    break;
	  case 0x20: { /* cvt.s */
	    mips_t mi(inst);
	    switch(mi.f.fmt)
	      {
	      case FMT_D:
		*reinterpret_cast<float*>(s->cpr1 + mi.f.fd) =
		  static_cast<float>(*reinterpret_cast<double*>(s->cpr1 + mi.f.fs));
		break;
	      case FMT_W:
		*reinterpret_cast<float*>(s->cpr1 + mi.f.fd) =
		  static_cast<float>(*reinterpret_cast<int32_t*>(s->cpr1 + mi.f.fs));
		break;
	      default:
		UNREACHABLE();
	      }
	    s->pc += 4;	    
	    break;
	  }
    	  case 0x21: { /* cvt.d */
	    mips_t mi(inst);
	    switch(mi.f.fmt)
	      {
	      case FMT_S:
		*reinterpret_cast<double*>(s->cpr1 + mi.f.fd) =
		  static_cast<double>(*reinterpret_cast<float*>(s->cpr1 + mi.f.fs));
		break;
	      case FMT_W:
		*reinterpret_cast<double*>(s->cpr1 + mi.f.fd) =
		  static_cast<double>(*reinterpret_cast<int32_t*>(s->cpr1 + mi.f.fs));
		break;
	      default:
		UNREACHABLE();
	      }
	    s->pc += 4;	    
	    break;
	  }
	  default:
	    printf("unhandled coproc1 instruction (%x) @ %08x\n", inst, s->pc);
	    exit(-1);
	    break;
	  }
      }
    }
 }

template <typename T>
struct c1xExec {
  void operator()(coproc1x_t insn, state_t *s) {
    T _fr = *reinterpret_cast<T*>(s->cpr1+insn.fr);
    T _fs = *reinterpret_cast<T*>(s->cpr1+insn.fs);
    T _ft = *reinterpret_cast<T*>(s->cpr1+insn.ft);
    T &_fd = *reinterpret_cast<T*>(s->cpr1+insn.fd);  
    switch(insn.id)
      {
      case 4:
	_fd = _fs*_ft + _fr;
	break;
      case 5:
	_fd = _fs*_ft - _fr;
	break;
      default:
	std::cerr << "unhandled coproc1x insn @ 0x"
		  << std::hex << s->pc << std::dec
		  << ", id = " << insn.id
		  <<"\n";
	exit(-1);
      }
    s->pc += 4;
  }
};


template <bool EL, typename T>
void lxc1(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  uint32_t ea = s->gpr[mi.lc1x.base] + s->gpr[mi.lc1x.index];
  *reinterpret_cast<T*>(s->cpr1 + mi.lc1x.fd) = bswap<EL>(*reinterpret_cast<T*>(s->mem + ea));
  s->pc += 4;
}


template <bool appendIns,bool EL>
void execCoproc1x(uint32_t inst, state_t *s) {
  mips_t mi(inst);

  switch(mi.lc1x.id)
    {
    case 0:
      //lwxc1
      lxc1<EL,int32_t>(inst, s);
      return;
    case 1:
      //ldxc1
      lxc1<EL,int64_t>(inst, s);
      return;
    default:
      break;
    }
  
  switch(mi.c1x.fmt)
   {
   case 0: {
     c1xExec<float> e;
     e(mi.c1x, s);
     return;
   }
   case 1: {
     c1xExec<double> e;
     e(mi.c1x, s);
     return;
   }
   default:
     std::cerr << "weird type in do_c1x_op @ 0x"
	       << std::hex << s->pc << std::dec
	       <<"\n";
     print_var(mi.c1x.fmt);
     exit(-1);
   }
}



template <bool EL>
void _sc(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  itype_store_helper<EL,int32_t>(mi, s);
  s->gpr[mi.i.rt] = 1;
  s->pc += 4;
}


void mkMonitorVectors(state_t *s) {
  for (uint32_t loop = 0; (loop < IDT_MONITOR_SIZE); loop += 4) {
      uint32_t vaddr = IDT_MONITOR_BASE + loop;
      uint32_t insn = (RSVD_INSTRUCTION |
			 (((loop >> 2) & RSVD_INSTRUCTION_ARG_MASK)
			  << RSVD_INSTRUCTION_ARG_SHIFT));
      /* printf("reserved isns = %x\n", insn); */
      *(uint32_t*)(s->mem+vaddr) = globals::isMipsEL ? bswap<true>(insn) : bswap<false>(insn);
  }
}



template <bool EL>
void _swl(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  uint32_t ea = s->gpr[mi.i.rs] + signExtendImm(mi.i);
  uint32_t ma = ea & 3;
  ea &= 0xfffffffc;
  if(EL) {
    ma = 3 - ma;
  }
  uint32_t r = bswap<EL>(*((int32_t*)(s->mem + ea))); 
  uint32_t xx=0,x = s->gpr[mi.i.rt];
  
  uint32_t xs = x >> (8*ma);
  uint32_t m = ~((1U << (8*(4 - ma))) - 1);
  xx = (r & m) | xs;
  *reinterpret_cast<uint32_t*>(s->mem + ea) = bswap<EL>(xx);
  s->pc += 4;
}

template <bool EL>
void _swr(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  uint32_t ea = s->gpr[mi.i.rs] + signExtendImm(mi.i);
  uint32_t ma = ea & 3;
  if(EL) {
    ma = 3 - ma;
  }
  ea &= 0xfffffffc;
  uint32_t r = bswap<EL>(*((int32_t*)(s->mem + ea))); 
  uint32_t xx=0,x = s->gpr[mi.i.rt];
  
  uint32_t xs = 8*(3-ma);
  uint32_t rm = (1U << xs) - 1;

  xx = (x << xs) | (rm & r);
  *reinterpret_cast<uint32_t*>(s->mem + ea) = bswap<EL>(xx);
  s->pc += 4;
}

template <bool EL>
void _lwl(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  uint32_t ea = ((uint32_t)s->gpr[mi.i.rs] + signExtendImm(mi.i));
  uint32_t ma = ea & 3;
  ea &= 0xfffffffc;
  if(EL) {
    ma = 3 - ma;
  }
  int32_t r = bswap<EL>(*((int32_t*)(s->mem + ea))); 
  switch(ma&3)
    {
    case 0:
      s->gpr[mi.i.rt] = r;
      break;
    case 1:
      s->gpr[mi.i.rt] = ((r & 0x00ffffff) << 8) | (s->gpr[mi.i.rt] & 0x000000ff) ;
      break;
    case 2:
      s->gpr[mi.i.rt] = ((r & 0x0000ffff) << 16) | (s->gpr[mi.i.rt] & 0x0000ffff) ;
      break;
    case 3:
      s->gpr[mi.i.rt] = ((r & 0x00ffffff) << 24) | (s->gpr[mi.i.rt] & 0x00ffffff);
      break;
    }
  s->pc += 4;
}

template <bool EL>
void _lwr(uint32_t inst, state_t *s) {
  mips_t mi(inst);  
  uint32_t ea = ((uint32_t)s->gpr[mi.i.rs] + signExtendImm(mi.i));
  uint32_t ma = ea & 3;
  ea &= 0xfffffffc;
  if(EL) {
    ma = 3-ma;
  }
  uint32_t r = bswap<EL>(*reinterpret_cast<int32_t*>(s->mem + ea)); 
  switch(ma & 3)
    {
    case 0:
      s->gpr[mi.i.rt] = (s->gpr[mi.i.rt] & 0xffffff00) | (r>>24);
      break;
    case 1:
      s->gpr[mi.i.rt] = (s->gpr[mi.i.rt] & 0xffff0000) | (r>>16);
      break;
    case 2:
      s->gpr[mi.i.rt] = (s->gpr[mi.i.rt] & 0xff000000) | (r>>8);
      break;
    case 3:
      s->gpr[mi.i.rt] = r;
      break;
    }
  s->pc += 4;
}




static void _truncw(uint32_t inst, state_t *s) {
  uint32_t fmt = (inst >> 21) & 31;
  uint32_t fd = (inst>>6) & 31;
  uint32_t fs = (inst>>11) & 31;
  switch(fmt)
    {
    case FMT_S:
      *reinterpret_cast<int32_t*>(s->cpr1 + fd) =
	static_cast<int32_t>(*reinterpret_cast<float*>(s->cpr1 + fs));
      break;
    case FMT_D:
      *reinterpret_cast<int32_t*>(s->cpr1 + fd) =
	static_cast<int32_t>(*reinterpret_cast<double*>(s->cpr1 + fs));      
      break;
    default:
      UNREACHABLE();
    }
  s->pc += 4;
}

template <typename T, bool ZC>
void _fpcmov(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  bool Z = (s->gpr[mi.c1x.ft] == 0);
  T* dst = reinterpret_cast<T*>(&s->cpr1[mi.c1x.fd]);
  T* src = reinterpret_cast<T*>(&s->cpr1[mi.c1x.fs]);
  if(ZC)
    *dst = Z ? *src : *dst;
  else
    *dst = Z ? *dst : *src;
  s->pc += 4;
}

template<bool fmovz>
static void _fmov(uint32_t inst, state_t *s) {
  switch((inst >> 21) & 31)
    {
    case FMT_S:
      _fpcmov<uint32_t,fmovz>(inst, s);
      break;
    case FMT_D:
      _fpcmov<uint64_t,fmovz>(inst, s);
      break;
    default:
      UNREACHABLE();
    }
}


static void _fmovc(uint32_t inst, state_t *s) {
  uint32_t cc = (inst >> 18) & 7;
  uint32_t fd = (inst>>6) & 31;
  uint32_t fs = (inst>>11) & 31;
  uint32_t tf = (inst>>16) & 1;
  switch((inst >> 21) & 31)
    {
    case FMT_S: {
      if(tf==0)
	s->cpr1[fd+0] = extractBit(s->fcr1[CP1_CR25], cc) ?
	  s->cpr1[fd+0] : s->cpr1[fs+0];
      else
	s->cpr1[fd+0] = extractBit(s->fcr1[CP1_CR25], cc) ?
	  s->cpr1[fs+0] : s->cpr1[fd+0];
      break;
    }
    case FMT_D: {
      if(tf==0) {
	if(not(extractBit(s->fcr1[CP1_CR25], cc))) {
	  s->cpr1[fd+0] = s->cpr1[fs+0];
	  s->cpr1[fd+1] = s->cpr1[fs+1];
	}
      }
      else {
	if(extractBit(s->fcr1[CP1_CR25], cc)) {
	  s->cpr1[fd+0] = s->cpr1[fs+0];
	  s->cpr1[fd+1] = s->cpr1[fs+1];
	}
      }      
      break;
    }
    default:
      UNREACHABLE();
    }
  s->pc += 4;  
}


template <typename T>
static void fpCmp(uint32_t inst, state_t *s) {
  mips_t mi(inst);
  uint32_t cond = inst & 15;
  uint32_t cc = (inst >> 8) & 7;
  T Tfs = *reinterpret_cast<T*>(s->cpr1+mi.f.fs);
  T Tft = *reinterpret_cast<T*>(s->cpr1+mi.f.ft);
  assert(std::isnan(Tfs)==false);
  assert(std::isnan(Tft)==false);

  switch(cond)
    {
    case COND_UN:
      s->fcr1[CP1_CR25] = setBit(s->fcr1[CP1_CR25],(Tfs==Tft),cc);
      break;
    case COND_EQ:
    case COND_UEQ:
      s->fcr1[CP1_CR25] = setBit(s->fcr1[CP1_CR25],(Tfs==Tft),cc);
      break;
    case COND_LT:
      s->fcr1[CP1_CR25] = setBit(s->fcr1[CP1_CR25],(Tfs<Tft),cc);
      break;
    case COND_LE:
    case COND_ULE:      
      s->fcr1[CP1_CR25] = setBit(s->fcr1[CP1_CR25],(Tfs<=Tft),cc);
      break;
    default:
      printf("unimplemented %s = %s\n", __func__, getCondName(cond).c_str());
      exit(-1);
      break;
    }
  s->pc += 4;
}


static void _c(uint32_t inst, state_t *s) {
  uint32_t fmt = (inst >> 21) & 31;
  switch(fmt)
    {
    case FMT_S:
      fpCmp<float>(inst, s);
      break;
    case FMT_D:
      fpCmp<double>(inst, s);
      break;
    default:
      UNREACHABLE();
    }
}

template <bool appendIns, bool EL>
void execMips(state_t *s) {
  uint8_t *mem = s->mem;
  uint32_t inst = bswap<EL>(*reinterpret_cast<uint32_t*>(mem + s->pc));
  
  if(appendIns) globals::cBB->addIns(inst, s->pc);
  s->icnt++;

  switch(getInsnType(inst))
    {
    case mips_type::rtype:
      execRType<appendIns,EL>(inst,s);
      break;
    case mips_type::itype:
      execIType<appendIns,EL>(inst,s);
      break;
    case mips_type::jtype:
      execJType<appendIns,EL>(inst,s);
      break;
    case mips_type::cp0:
      execCoproc0<appendIns>(inst,s);
      break;
    case mips_type::cp1:
      execCoproc1<appendIns,EL>(inst,s);
      break;
    case mips_type::cp1x:
      execCoproc1x<appendIns,EL>(inst,s);
      break;
    case mips_type::cp2:
      UNREACHABLE();
      break;
    case mips_type::special2:
      execSpecial2<appendIns>(inst,s);
      break;
    case mips_type::special3:
      execSpecial3<appendIns>(inst,s);
      break;
    case mips_type::ll:
      /* use lw as a proxy */
      exec_itype<itypeOperation::_lw, EL>(inst,s);
      break;
    case mips_type::sc:
      _sc<EL>(inst, s);
      break;
    default:
      UNREACHABLE();
    }

  if(s->gpr[0] != 0) {
    printf("pc=%x, s->gpr[0] = %x\n", s->pc, s->gpr[0]);
    exit(-1);
  }
}

void interpretAndBuildCFG(state_t *s) {
  execMips<true,false>(s);
}

void interpret(state_t *s) {
  execMips<false,false>(s);
}

void interpretAndBuildCFGEL(state_t *s) {
  execMips<true,true>(s);
}

void interpretEL(state_t *s) {
  execMips<false,true>(s);
}
