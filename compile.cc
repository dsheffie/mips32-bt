#include <cstdint>     
#include <cstdio>
#include "compile.hh"
#include "mips.hh"
#define ELIDE_LLVM
#include "globals.hh"
#include <unordered_set>

bool compile::canCompileInstr(uint32_t inst) {
  uint32_t opcode = inst>>26;
  bool isRType = (opcode==0);
  bool isJType = ((opcode>>1)==1);
  bool isCoproc0 = (opcode == 0x10);
  bool isCoproc1 = (opcode == 0x11);
  bool isCoproc1x = (opcode == 0x13);
  bool isCoproc2 = (opcode == 0x12);
  bool isSpecial2 = (opcode == 0x1c); 
  bool isSpecial3 = (opcode == 0x1f);
  bool isLoadLinked = (opcode == 0x30);
  bool isStoreCond = (opcode == 0x38);
  bool rc = false;

  if(isRType)
    rc = canCompileRType(inst);
  else if(isSpecial2)
    rc = canCompileSpecial2(inst);
  else if(isSpecial3)
    rc = canCompileSpecial3(inst);
  else if(isJType)
    rc = canCompileJType(inst);
  else if(isCoproc0)
    return false;
  else if(isCoproc1)
    rc = canCompileCoproc1(inst);
  else if(isCoproc1x)
    rc = canCompileCoproc1x(inst);
  else if(isCoproc2)
    return false;
  else if(isLoadLinked)
    return false;
  else if(isStoreCond)
    return false;
  else 
    rc = canCompileIType(inst);
  
  return rc;
}

bool compile::canCompileRType(uint32_t inst) {
  uint32_t funct = inst & 63;
  bool rc = false;
  switch(funct)
    {
    case 0x05:
      //monitor
      rc = false;
      break;
    case 0x08: //jr
    case 0x09: //jalr
      rc = globals::ipo;
      break;
    case 0x0C: //syscall
    case 0x0D: //break
      rc = false;
      break;
    case 0x0F:
      //sync
      rc = false;
      break;
    case 0x30: 
      //tge
      rc = false;
      break;
    default:
      rc = true;
      break;
    }
  return rc;
}

bool compile::canCompileSpecial2(uint32_t inst) {
  uint32_t funct = inst & 63;
  static const std::unordered_set<uint8_t> ops =
    {0x0 /*madd*/, 0x1 /*maddu*/,
     0x2 /*mul*/, 0x4, 0x20};
  auto it = ops.find(funct);
  return it != ops.end();
}

bool compile::canCompileSpecial3(uint32_t inst) {
  uint32_t funct = inst & 63;
  uint32_t op = (inst>>6) & 31;
  bool rc = false;
  if(funct == 32)
    {
      switch(op)
	{
	case 0x10:
	  //seb
	  rc = true;
	  break;
	case 0x18:
	  //seh
	  rc = true;
	  break;
	default:
	  rc = false;
	  break;
	}
    }
  /* EXT instruction */
  else if(funct == 0) {
    rc = true;//ext
  }
  else if(funct == 0x4) {
    rc = true;//ins
  }
  else { 
    rc = false;
  }
  return rc;
}



bool compile::canCompileCoproc1(uint32_t inst) {
  uint32_t opcode = inst>>26;
  uint32_t functField = (inst>>21) & 31;
  uint32_t lowop = inst & 63;  
  uint32_t fmt = (inst >> 21) & 31;
  uint32_t nd_tf = (inst>>16) & 3;
  
  uint32_t lowbits = inst & ((1<<11)-1);
  opcode &= 0x3;
  bool rc = false;

  if(fmt == 0x8)
    {
      switch(nd_tf)
	{
	case 0x0:
	  //bc1f
	  rc = true;
	  break;
	case 0x1:
	  //bc1t
	  rc = true;
	  break;
	case 0x2:
	  //bc1fl
	  rc = true;
	  break;
	case 0x3:
	  //bc1tl
	  rc = true;
	  break;
	}
      /*BRANCH*/
    }
  else if((lowbits == 0) && ((functField==0x0) || (functField==0x4)))
    {
      if(functField == 0x0)
	{
	  /* move from coprocessor */
	  //mfc1
	  /* NEED INVARIANT CHECKING CODE */
	  rc = true;
	}
      else if(functField == 0x4)
	{
	  /* move to coprocessor */
	  //mtc1
	  rc = true;
	}
    }
  else
    {
      if((lowop >> 4) == 3)
	{
	  //c
	  rc = true;
	}
      else
	{
	  uint32_t fmt = (inst >> 21) & 31;
	  switch(lowop)
	    {
	    case 0x0:
	      //fadd
	      rc = (fmt==FMT_S) || (fmt==FMT_D);
	      break;
	    case 0x1:
	      //fsub
	      rc = (fmt==FMT_S) || (fmt==FMT_D);
	      break;
	    case 0x2:
	      //fmul
	      rc = (fmt==FMT_S) || (fmt==FMT_D);
	      break;
	    case 0x3:
	      //fdiv
	      rc = (fmt==FMT_S) || (fmt==FMT_D);
	      break;
	    case 0x4:
	      //fsqrt
	      rc = (fmt==FMT_S) || (fmt==FMT_D);
	      break;
	    case 0x6:
	      //fmov
	      rc = (fmt==FMT_S) || (fmt==FMT_D);
	      break;
	    case 0x9:
	      //truncl
	      rc = false;
	      break;
	    case 0xd:
	      //truncw
	      rc = true;
	      break;
	    case 0x11:
	      //fmovc
	      rc = (fmt==FMT_S) || (fmt==FMT_D);
	      break;
	    case 0x20:
	      //cvts
	      rc = true;
	      break;
	    case 0x21:
	      //cvtd
	      rc = true;
	      break;
	    default:
	      break;
	    }
	}
    }
  
  return rc;
}

 
bool compile::canCompileCoproc1x(uint32_t inst) {
  mips_t mi(inst);
  switch(mi.lc1x.id)
    {
    case 0:
    case 1:
      return false;
    default:
      break;
    }
  switch(mi.c1x.id)
   {
   //fmadd
   case 4:
     return true;
     //fmsub
   case 5:
     return true;
   default:
     break;
   }
  return false;
}

bool compile::canCompileJType(uint32_t inst) {
  uint32_t opcode = inst>>26;
  bool rc = false;
  if(opcode==0x2) {
    //j
    rc = true;
  }
  else if(opcode==0x3) {
    //jal
    rc = true;
  }
  return rc;
}

bool compile::canCompileIType(uint32_t inst) {
  uint32_t opcode = inst>>26;
  static const std::unordered_set<uint8_t> ops =
    {0x01,0x04,0x05,0x06,
     0x07,0x08,0x09,0x0a,
     0x0b,0x0c,0x0d,0x0e,
     0x0f,0x14,0x15,0x16,
     0x17,0x20,0x21,0x22,
     0x23,0x24,0x25,0x26,
     0x28,0x29,0x2a,0x2b, 
     0x2e,0x31,0x35,0x39,
     0x3d};
  return ops.find(opcode) != ops.end();
}

