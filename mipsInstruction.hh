#ifndef __SIM_MIPS_INSN__
#define __SIM_MIPS_INSN__

#include <string>
#include <cstdint>
#include "ssaInsn.hh"
#include "llvmInc.hh"

class cfgBasicBlock;
class regionCFG;
class llvmRegTables;

class Insn;
enum regEnum {uninit=0,constant,variant};
enum opPrecType {integerprec=0,singleprec,doubleprec,fpspecialprec,unknownprec,dummyprec};

struct regState {
  regEnum e;
  uint32_t v;
};

Insn* getInsn(uint32_t inst, uint32_t addr);

class Insn : public ssaInsn {
protected:
  friend std::ostream &operator<<(std::ostream &out, const Insn &ins);
  uint32_t inst, addr;
  regionCFG *cfg = nullptr;
  cfgBasicBlock *myBB = nullptr;
  
public:
  llvm::Value *byteSwap(llvm::Value *v);

  void saveInstAddress();
  void set(regionCFG *cfg, cfgBasicBlock *cBB);
  void emitPrintPC();
  size_t getInsnId();
  bool codeGen(cfgBasicBlock *cBB, Insn* nInst, llvmRegTables& regTbl);
  std::string getString() const;
  
  virtual bool generateIR(cfgBasicBlock *cBB, Insn* nInst, llvmRegTables& regTbl);
  virtual void updateGPRConstants(std::vector<regState> &gprConstState);
  virtual void recDefines(cfgBasicBlock *cBB, regionCFG *cfg);
  virtual void recUses(cfgBasicBlock *cBB);
  
  virtual opPrecType getPrecType() const {
    return integerprec;
  }
  virtual bool canCompile() const {
    return true;
  }  
  virtual bool isLikelyBranch() const {
    return false;
  }  
  uint32_t getAddr() const {
    return addr;
  }
  Insn(uint32_t inst, uint32_t addr, insnDefType insnType = insnDefType::unknown) :
    ssaInsn(insnType), inst(inst), addr(addr) {
  }
  virtual ~Insn() {}
};

class abstractBranch {
public:
 virtual uint32_t getTakenAddr() const = 0;
 virtual uint32_t getNotTakenAddr() const = 0; 
};

class iTypeInsn : public Insn  {
protected:
  uint32_t rs, rt;
  int32_t simm;
  uint32_t uimm;
public:
  iTypeInsn(uint32_t inst, uint32_t addr, insnDefType insnType = insnDefType::gpr) : 
    Insn(inst, addr, insnType),
    rs((inst >> 21) & 31),
    rt((inst >> 16) & 31){
    int16_t himm = (int16_t)(inst & ((1<<16) - 1));
    simm = (int32_t)himm;
    uimm = inst & ((1<<16) - 1);
  }
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override;
  void recUses(cfgBasicBlock *cBB) override;
};

class iBranchTypeInsn : public iTypeInsn, public abstractBranch {
protected:
 bool isLikely;
 int32_t tAddr=0,ntAddr=0;
public:
 iBranchTypeInsn(uint32_t inst, uint32_t addr) : 
   iTypeInsn(inst, addr, insnDefType::no_dest) {
   simm <<= 2;
   tAddr = simm + addr + 4;
   ntAddr = addr + 8;
   isLikely=false;
 }
  uint32_t getTakenAddr() const override { 
    return tAddr; 
  }
  uint32_t getNotTakenAddr() const override { 
    return ntAddr; 
  }
  bool isLikelyBranch() const override {
    return isLikely;
  }
  void updateGPRConstants(std::vector<regState> &gprConstState) override {/*no writes*/}
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override ;
  void recUses(cfgBasicBlock *cBB) override;
  virtual bool handleBranch(cfgBasicBlock *cBB, Insn* nInst, llvmRegTables& regTbl,
			    llvm::CmpInst::Predicate branchPred,
			    llvm::Value *vRT, llvm::Value *vRS);
};


class rTypeInsn : public Insn {
 protected:
 uint32_t rd, rs, rt;
 public:
  rTypeInsn(uint32_t inst, uint32_t addr, insnDefType insnType = insnDefType::gpr) :
   Insn(inst, addr, insnType),
   rd((inst >> 11) & 31),
   rs((inst >> 21) & 31),
   rt((inst >> 16) & 31){}
 void updateGPRConstants(std::vector<regState> &gprConstState) override ;
 void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override;
 void recUses(cfgBasicBlock *cBB) override;
 virtual uint32_t doOp(uint32_t a, uint32_t b) {
  return ~0;
 }
};

class rTypeJumpRegInsn : public rTypeInsn {
public:
  rTypeJumpRegInsn(uint32_t inst, uint32_t addr, insnDefType insnType = insnDefType::no_dest) :
    rTypeInsn(inst, addr, insnType) {};
};

class jTypeInsn : public Insn {
 protected:
 uint32_t jaddr;
 public:
  jTypeInsn(uint32_t inst, uint32_t addr, insnDefType insnType = insnDefType::no_dest) :
    Insn(inst, addr, insnType) {
    jaddr = inst & ((1<<26)-1);
    jaddr <<= 2;
    jaddr |= ((addr+4) & (~((1<<28)-1)));
  }
  uint32_t getJumpAddr() const {
    return jaddr;
  }
  void updateGPRConstants (std::vector<regState> &gprConstState) override {}
};

class insn_j : public jTypeInsn {
public:
  insn_j(uint32_t inst, uint32_t addr) : jTypeInsn(inst, addr) {}
  bool generateIR(cfgBasicBlock *cBB, Insn* nInst, llvmRegTables& regTbl) override;
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override {}
  void recUses(cfgBasicBlock *cBB) override {}
  uint32_t destRegister() const override {
    return 0;
  }
};

class insn_jal : public jTypeInsn {
public:
  insn_jal(uint32_t inst, uint32_t addr) :
    jTypeInsn(inst, addr, insnDefType::gpr) {}
  bool generateIR(cfgBasicBlock *cBB, Insn* nInst, llvmRegTables& regTbl) override;
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override;
  void recUses(cfgBasicBlock *cBB) override {}
  uint32_t destRegister() const override {
    return 31;
  }
  void updateGPRConstants(std::vector<regState> &gprConstState) override {
    gprConstState[31].e = variant; 
  }
  
};


class insn_jr : public rTypeJumpRegInsn {
public:
  insn_jr(uint32_t inst, uint32_t addr) :
    rTypeJumpRegInsn(inst, addr) {}
  bool generateIR(cfgBasicBlock *cBB, Insn* nInst, llvmRegTables& regTbl) override;
  bool canCompile() const override;
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override {}
  void recUses(cfgBasicBlock *cBB) override;
};

class insn_jalr : public rTypeJumpRegInsn {
public:
  insn_jalr(uint32_t inst, uint32_t addr) :
    rTypeJumpRegInsn(inst, addr, insnDefType::gpr) {}
  bool canCompile() const override;
  bool generateIR(cfgBasicBlock *cBB, Insn* nInst, llvmRegTables& regTbl) override;
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override;
  void recUses(cfgBasicBlock *cBB) override;
};

class coprocType1Insn : public Insn {
protected:
  uint32_t fmt, ft, fs, fd;
public:
  coprocType1Insn(uint32_t inst, uint32_t addr);
  bool isFloatingPoint() const override {
    return true;
  }
  opPrecType getPrecType() const override;
  bool canCompile() const override;
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override;
  void recUses(cfgBasicBlock *cBB) override;
};


class fpBranchInsn : public coprocType1Insn, public abstractBranch {
protected:
  uint32_t cc;
  int16_t himm;
  uint32_t tAddr, ntAddr;
public:
  fpBranchInsn(uint32_t inst, uint32_t addr);
  uint32_t getTakenAddr() const override { 
    return tAddr; 
  }
  uint32_t getNotTakenAddr() const override { 
    return ntAddr; 
  }
  void recUses(cfgBasicBlock *cBB) override;
  void recDefines(cfgBasicBlock *cBB, regionCFG *cfg) override;
  opPrecType getPrecType() const override {
    return fpspecialprec;
  }
};

#endif
