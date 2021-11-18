#ifndef __REGION_CFG_HH__
#define __REGION_CFG_HH__

#include <map>
#include <unordered_map>
#include <set>
#include <bitset>
#include <vector>
#include <list>
#include <array>
#include <cstdint>
#include <limits.h>

#include "execUnit.hh"
#include "basicBlock.hh"
#include "ssaInsn.hh"
#include "mipsInstruction.hh"
#include "llvmInc.hh"
#include "perfmap.hh"
#include "debugSymbols.hh"

class regionCFG;
class Insn;
class cfgBasicBlock;
class llvmRegTables;

#define __fpr_state_list(m) \
  m(unused) \
  m(singlePrec) \
  m(doublePrec) \
  m(both)	\
  m(unknown)

#define __fpr_state_enum(m) m,
enum class fprUseEnum{__fpr_state_list(__fpr_state_enum)};
#undef __fpr_state_enum

inline std::ostream &operator<<(std::ostream &out, const fprUseEnum &fprState) {
#define __fpr_state_pair(m) {fprUseEnum::m, #m},
  std::map<fprUseEnum, std::string> fpr_state_map = {
    __fpr_state_list(__fpr_state_pair)
  };
  return out << fpr_state_map.at(fprState);
#undef __fpr_state_pair
}
#undef __fpr_state_list

typedef void (*compiledCFG)
(uint32_t*, /* pc */
 int32_t*, /* gpr */
 uint32_t*, /* cpr0 */
 uint32_t*, /*cpr1 */
 uint32_t*, /*fcr1 */
 uint8_t*, /* mem */
 int32_t*, /* lo */
 int32_t*, /* hi */
 uint64_t*, /* icnt */
 uint64_t*, /* abortloc */
 uint64_t*  /* nextbb */
 );


class phiNode : public ssaInsn {
 protected:
  llvm::PHINode *lPhi;
  std::set<cfgBasicBlock*> inBoundEdges;
 public:
  phiNode(insnDefType insnType = insnDefType::unknown) :
    ssaInsn(insnType), lPhi(nullptr){}
  virtual ~phiNode() {}
  llvm::PHINode *get() const { return lPhi; }
  virtual void makeLLVMPhi(regionCFG *cfg,llvmRegTables& regTbl) {}
  bool parentInLLVM(cfgBasicBlock *b);
  llvm::BasicBlock *getLLVMParentBlock(cfgBasicBlock *b);
  virtual void print() const = 0;
  virtual void addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b)  = 0;
};

class gprPhiNode : public phiNode {
 protected:
  uint32_t gprId;
 public:
  gprPhiNode(uint32_t gprId) : phiNode(insnDefType::gpr), gprId(gprId){}
  uint32_t destRegister() const override {
    return gprId;
  }
  void makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) override;
  void addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) override;
  void print() const override {
    printf("phi for gpr %u\n", gprId);
  }
};

class hiloPhiNode : public phiNode {
protected:
  uint32_t hi;
public:
  hiloPhiNode(uint32_t hi) : phiNode(insnDefType::hilo), hi(hi){}
  uint32_t destRegister() const override {
    return hi;
  }
  void makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) override;
  void addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) override;
  void print() const override {
    printf("phi for hi/lo \n");
  }
};

class fprPhiNode : public phiNode {
 protected:
  uint32_t fprId;
  fprUseEnum useType;
 public:
  fprPhiNode(uint32_t fprId, fprUseEnum useType = fprUseEnum::unknown) :
    phiNode(insnDefType::fpr), fprId(fprId), useType(useType) {}
  uint32_t destRegister() const override {
    return fprId;
  }
  void setUseType(fprUseEnum useType) {
    this->useType = useType;
  }
  fprUseEnum getUseType() const {
    return useType;
  }
  void makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) override;
  void addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) override;
  bool isFloatingPoint() const override {
    return true;
  }
  void print() const override {
    printf("phi for fpr %u\n", fprId);
  }
};

class fcrPhiNode : public phiNode {
 protected:
  uint32_t fcrId;
 public:
  fcrPhiNode(uint32_t fcrId) : phiNode(insnDefType::fcr), fcrId(fcrId) {}
  uint32_t destRegister() const override {
    return fcrId;
  }
  void makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) override;
  void addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) override;
  void print() const override {
    printf("phi for fcr %u\n", fcrId);
  }
};

class icntPhiNode : public phiNode {
public:
  icntPhiNode(uint32_t id=0) : phiNode(insnDefType::icnt) {}
  void makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) override;
  void addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) override;
  void print() const override {
    printf("%s\n", __PRETTY_FUNCTION__);
  }
};

class llvmRegTables : public MipsRegTable<llvm::Value> {
public:
  regionCFG *cfg = nullptr;
  llvm::IRBuilder<> *myIRBuilder = nullptr;
  llvm::Value *iCnt = nullptr;
  void initIcnt(); 
  void incrIcnt(size_t amt); 
  llvmRegTables(regionCFG *cfg);
  llvmRegTables();
  llvm::Value *loadGPR(uint32_t gpr);
  llvm::Value *setGPR(uint32_t gpr, uint32_t x);
  llvm::Value *loadFPR(uint32_t fpr);
  llvm::Value *loadHiLo(uint32_t h);
  llvm::Value *loadFCR(uint32_t fcr);
  llvm::Value *getFPR(uint32_t fpr, fprUseEnum useType=fprUseEnum::unused);
  void setFPR(uint32_t fpr, llvm::Value *v);
  void storeGPR(uint32_t gpr);
  void storeFPR(uint32_t fpr);
  void storeHiLo(uint32_t h);
  void storeFCR(uint32_t fcr);
  void storeIcnt();
  llvm::Value *getIcnt() {
    return iCnt;
  }
  llvm::IRBuilder<> &getBuilder() const {
    assert(myIRBuilder);
    return *myIRBuilder;
  }
  void copy(const llvmRegTables &other);
};


std::ostream &operator<<(std::ostream &out, const cfgBasicBlock &bb);

class cfgBasicBlock {
 public:
  friend std::ostream &operator<<(std::ostream &out, const cfgBasicBlock &bb);
  friend class regionCFG;
  basicBlock *bb;
  bool isLikelyPatch;
  bool hasTermBranchOrJump;
  llvmRegTables termRegTbl;
  llvm::BasicBlock *lBB;
  cfgBasicBlock *idombb;
  std::set<cfgBasicBlock*> dtree_succs;

  std::vector<phiNode*> phiNodes;
  std::array<phiNode*,32> gprPhis;
  std::array<phiNode*,32> fprPhis;
  std::array<phiNode*,5> fcrPhis;
  std::array<phiNode*,2> hiLoPhis;
  std::array<phiNode*,1> icntPhis;

  std::map<llvm::BasicBlock*, llvm::BasicBlock*> jrMap;

  std::vector<regState> gprConstState;
  std::set<cfgBasicBlock*> preds;
  std::set<cfgBasicBlock*> succs;
  std::set<cfgBasicBlock*> dfrontier;
  std::vector<basicBlock::insPair> rawInsns;
  std::vector<Insn*> insns;
  std::vector<ssaInsn*> ssaInsns;

  
  std::bitset<32> gprRead;
  std::bitset<32> hiloRead;
  std::bitset<32> fprRead;
  std::bitset<5> fcrRead;

  std::vector<fprUseEnum> fprTouched;
  
  ssize_t dt_dfn = -1;
  ssize_t dt_max_ancestor_dfn = -1;

  
  llvm::BasicBlock *getSuccLLVMBasicBlock(uint32_t pc);
  void addPhiNode(gprPhiNode *phi);
  void addPhiNode(hiloPhiNode *phi);
  void addPhiNode(fprPhiNode *phi);
  void addPhiNode(fcrPhiNode *phi);
  void addPhiNode(icntPhiNode *phi);
  bool checkIfPlausableSuccessors();
  void addWithInCFGEdges(regionCFG *cfg);
  bool has_jr_jalr();
  bool haslikely();
  bool canCompile() const;
  bool hasFloatingPoint(uint32_t *typeCnts) const;
  void print();
  uint32_t getExitAddr() const;
  uint32_t getEntryAddr() const;
  std::string getName() const;
  void computeConstGPRs();

  void traverseAndRename(regionCFG *cfg);  
  void traverseAndRename(regionCFG *cfg, llvmRegTables regTbl);
  
  void patchUpPhiNodes(regionCFG *cfg);
  void bindInsns(regionCFG *cfg);
  
  cfgBasicBlock *splitBB(uint32_t splitPC);
  
  bool hasBranchLikely() {
    return bb->hasBranchLikely();
  }
  bool patchLikely(regionCFG *cfg, cfgBasicBlock *pbb);
  void addSuccessor(cfgBasicBlock *s);
  void delSuccessor(cfgBasicBlock *s);
  cfgBasicBlock(basicBlock *bb, bool isLikelyPatch=false);
  ~cfgBasicBlock();
  void updateFPRTouched(uint32_t reg, fprUseEnum useType);
  const std::set<cfgBasicBlock*> &getPreds() const {
    return preds;
  }
  size_t numSuccessors() const {
    return succs.size();
  }
  const std::set<cfgBasicBlock*> &getSuccs() const {
    return succs;
  }
  const std::set<cfgBasicBlock*> &getDTSuccs() const {
    return dtree_succs;
  }
  const std::vector<Insn*> &getInsns() const {
    return insns;
  }
  const std::vector<ssaInsn*> &getSSAInsns() const {
    return ssaInsns;
  }
  const basicBlock *getOldBB() const {
    return bb;
  }
  cfgBasicBlock* &getIdom() {
    return idombb;
  }
  uint64_t getOldInscnt() const {
    if(bb) {
      return bb->getInscnt();
    }
    return 0;
  }
  void addDTreeSucc(cfgBasicBlock *bb) {
    dtree_succs.insert(bb);
  }
  bool fastDominates(const cfgBasicBlock *B) const {
    /* Appel exercise 19.1 - constant time dominance */
    return  (this==B) || ((dt_dfn  < B->dt_dfn) && (dt_max_ancestor_dfn >= B->dt_dfn));
  }
  bool dominates(const cfgBasicBlock *B) const;
};

class naturalLoop {
private:
  friend class sortNaturalLoops;
  cfgBasicBlock *head;
  std::set<cfgBasicBlock*> loop;
public:
  naturalLoop(cfgBasicBlock *head, std::set<cfgBasicBlock*> loop) :
    head(head), loop(loop) {}
  bool inSingleBlockLoop(cfgBasicBlock *blk) {
    if(loop.size() != 1)
      return false;
    return (head == blk);
  }
  void print() {
    printf("%s ", head->getName().c_str());
    for(cfgBasicBlock *blk : loop) {
      if(blk != head) printf("%s ", blk->getName().c_str());
    }
    printf("\n");
  }
  bool isNestedLoop(naturalLoop &other) {
    /* Does the head of loop "other" dominate the head 
     * of this loop */
    return head->dominates(other.head);
  }
  bool operator<(const naturalLoop &other) const {
    return loop.size() < other.loop.size();
  }
  const std::set<cfgBasicBlock*> &getLoop() const {
    return loop;
  }
};

class sortNaturalLoops {
public:
  bool operator() (const naturalLoop &lhs, 
		   const naturalLoop &rhs) const {
    return lhs.loop.size() > rhs.loop.size();
  }
};

std::ostream &operator<<(std::ostream &out, const regionCFG &cfg);
class regionCFG : public execUnit {
protected:
  /* definitions */
  const static int histoLen = 8;
  const static bool emitPCs = false;
  /* to be constructor list initialized */
  basicBlock *head = nullptr;
  cfgBasicBlock *cfgHead = nullptr;
  cfgBasicBlock *entryBlock = nullptr;
  cfgBasicBlock *innerPerfectBlock = nullptr;
  uint64_t uuid = 0,runs = 0, minIcnt = 0, maxIcnt = 0;
  double headProb = 0.0;
  bool isMegaRegion = false;
  bool perfectNest = false;
  bool hasBoth = false;
  bool validDominanceAcceleration = false;
  double compileTime = 0.0;
  
 public:
  friend std::ostream &operator<<(std::ostream &out, const regionCFG &cfg);
  static uint64_t icnt;
  static uint64_t iters;
  static std::set<regionCFG*> regionCFGs;
  std::unordered_map<std::string, llvm::Function*> builtinFuncts;

  void doLiveAnalysis(std::ostream &out) const;
  
  uint64_t &getuuid() {
    return uuid;
  }
  cfgBasicBlock* &getInnerPerfectBlock() {
    return innerPerfectBlock;
  }
  bool getEmitPCs() const {
    return emitPCs;
  }

  std::array<uint64_t, histoLen> runHistory;
  std::set<uint32_t> nextPCs;
  std::set<basicBlock*> blocks;
  compiledCFG codeBits;
  perfmap *pmap;
  llvm::LLVMContext *Context;
  std::map<std::string, llvm::Value*> blockArgMap;
  llvm::Function *blockFunction;
  llvm::IRBuilder<> *myIRBuilder;
  llvm::Module *myModule;
  llvm::EngineBuilder *myEngineBuilder; 
  llvm::ExecutionEngine *myExecEngine;
  llvm::Type *type_iPtr32,*type_iPtr8;
  llvm::Type *type_iPtr64,*type_void;
  llvm::Type *type_float, *type_double;
  llvm::Type *type_int32, *type_int64;
 
  std::set<cfgBasicBlock*> gprDefinitionBlocks[32];
  std::set<cfgBasicBlock*> hiloDefinitionBlocks;
  std::set<cfgBasicBlock*> fprDefinitionBlocks[32];
  std::set<cfgBasicBlock*> fcrDefinitionBlocks[5];

  std::bitset<32> allGprRead;
  std::bitset<1> allHiloRead;
  std::bitset<32> allFprRead;
  std::bitset<5> allFcrRead;
  std::vector<fprUseEnum> allFprTouched;


  std::vector< std::vector<naturalLoop> >loopNesting;


  std::vector<cfgBasicBlock*> cfgBlocks;
  std::map<uint32_t, cfgBasicBlock*> cfgBlockMap;

  void splitBBs();
  bool allBlocksReachable(cfgBasicBlock *root);
  void computeDominance();
  void computeDominanceFrontiers();
  void computeLengauerTarjanDominance();
  void fastDominancePreComputation();
  void insertPhis();
  void getRegDefBlocks();
  void initLLVMAndGeneratePreamble();
  llvm::BasicBlock* generateAbortBasicBlock(uint32_t abortpc,
					    llvmRegTables& regTbl, 
					    cfgBasicBlock *cBB,
					    llvm::BasicBlock *lBB,
					    uint32_t locpc = 0);
  
  llvm::BasicBlock* generateAbortBasicBlock(llvm::Value *abortpc,
					    llvmRegTables& regTbl, 
					    cfgBasicBlock *cBB,
					    llvm::BasicBlock *lBB);
  regionCFG();
  ~regionCFG();
  bool buildCFG(std::vector<std::vector<basicBlock*> > &regions);

  bool analyzeGraph();
  void generateMachineCode( llvm::CodeGenOpt::Level optLevel);
  void runLLVMLoopAnalysis();
  void dumpLLVM();
  void dumpIR();
  void emulate(state_t *s);
  void print();
  void asDot() const;
  void findLoop(std::set<cfgBasicBlock*> &loop,
		std::list<cfgBasicBlock*> &stack, 
		cfgBasicBlock *hbb);
  void findNaturalLoops();
  bool dominates(cfgBasicBlock *A, cfgBasicBlock *B) const;
  static void dropCompiled();
  uint32_t getEntryAddr() const override;
  basicBlock* run(state_t *s) override;
  void report(std::string &s, uint64_t icnt) override;
  void info() override;
  void toposort(std::vector<cfgBasicBlock*> &topo) const;
  uint64_t countInsns() const;
  uint64_t countBBs() const;
  uint64_t numBBInCommon(const regionCFG &other) const;
};

#endif
