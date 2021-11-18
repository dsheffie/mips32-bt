#include <queue>
#include <algorithm>
#include <ostream>
#include <limits>
#include <fstream>
#include <boost/dynamic_bitset.hpp>
#include <fcntl.h>

#include "regionCFG.hh"
#include "helper.hh"
#include "disassemble.hh"
#include "debugSymbols.hh"
#include "globals.hh"

static regionCFG *currCFG = nullptr;

/* Implementation from Muchnick and Lengauer-Tarjan TOPLAS 
 * paper. Vague understanding from Appel. */
class LengauerTarjanDominators {
private:
  std::vector<cfgBasicBlock*> &blocks;
  cfgBasicBlock *root = nullptr;
  ssize_t dfsCounter = 1;
  /* Parent in the DFS tree */
  std::map<cfgBasicBlock*, cfgBasicBlock*> Parent;
  /* Ancestor chain in DFS tree */
  std::map<cfgBasicBlock*, cfgBasicBlock*> Ancestor;
  std::map<cfgBasicBlock*, cfgBasicBlock*> Label;
  std::map<cfgBasicBlock*, std::set<cfgBasicBlock*>> Buckets;
  std::map<ssize_t, cfgBasicBlock*> Ndfs;
  std::map<cfgBasicBlock*, ssize_t> Sdno;
  
  void DFS(cfgBasicBlock *bb) {
    Sdno.at(bb) = dfsCounter;
    Ndfs[dfsCounter] = bb;
    Label[bb] = bb;
    Ancestor[bb] = nullptr;
    dfsCounter++;
    for(cfgBasicBlock *nbb : bb->getSuccs()) {
      if(Sdno.at(nbb) == 0) {
	Parent[nbb] = bb;
	DFS(nbb);
      }
    }
  }
  /* these methods are from Lengauer-Tarjan paper
   * and implement O(n*lg(n)) scheme */
  void Link(cfgBasicBlock *v, cfgBasicBlock *w) {
    Ancestor.at(w) = v;
  }
  void Compress(cfgBasicBlock *bb) {
    if(Ancestor.at(Ancestor.at(bb))) {
      Compress(Ancestor.at(bb));
      if(Sdno.at(Label.at(Ancestor.at(bb))) < Sdno.at(Label.at(bb))) {
	Label.at(bb) = Label.at(Ancestor.at(bb));
      }
      Ancestor.at(bb) = Ancestor.at(Ancestor.at(bb));
    }
  }
  cfgBasicBlock *Eval(cfgBasicBlock *bb) {
    if(!Ancestor.at(bb)) {
      return bb;
    }
    else {
      Compress(bb);
      return Label.at(bb);
    }
  }
  
public:
  LengauerTarjanDominators(std::vector<cfgBasicBlock*> &blocks) : blocks(blocks) {}
  void operator()() {
    for(cfgBasicBlock *bb : blocks) {
      if(bb->getPreds().empty()) {
	assert(root==nullptr);
	root = bb;
      }
      Sdno[bb] = 0;
      Parent[bb] = nullptr;
      bb->getIdom() = nullptr;
    }
    assert(root);
    DFS(root);
    
    for(ssize_t i = (dfsCounter-1); i > 1; i--) {
      cfgBasicBlock *w = Ndfs.at(i);
      ssize_t &sdom_w = Sdno.at(w);
      for(cfgBasicBlock *pbb : w->getPreds()) {
	cfgBasicBlock *u = Eval(pbb);
	ssize_t sdom_u = Sdno.at(u);
	if(sdom_u < sdom_w) {
	  sdom_w = sdom_u;
	}
      }
      Buckets[Ndfs.at(sdom_w)].insert(w);
      Link(Parent.at(w), w);
      /* need to understand - why parent bucket? */
      std::set<cfgBasicBlock*> &pBucket = Buckets[Parent.at(w)];
      for(cfgBasicBlock *v : pBucket) {
	/* find ancestor with lowest semidominator */
	cfgBasicBlock *u = Eval(v);
	if(Sdno.at(u) < Sdno.at(v)) {
	  /* idom is the semidominator */
	  v->getIdom() = u;
	}
	else {
	  /* must defer */
	  v->getIdom() = Parent.at(w);
	}
      }
      pBucket.clear();
    }
    for(ssize_t i = 2; i < dfsCounter; i++) {
      cfgBasicBlock *w = Ndfs.at(i);
      if(w->getIdom() != Ndfs.at(Sdno.at(w))) {
	w->getIdom() = w->getIdom()->getIdom();
      }
    }
    for(cfgBasicBlock *bb : blocks) {
      if(bb->getPreds().empty()) {
	continue;
      }
      bb->getIdom()->addDTreeSucc(bb);
    }
  }
  
};


template <typename T>
class sortByIcnt {
public:
  bool operator() (const T& lhs, const T& rhs) const {
    return lhs->getInscnt() > rhs->getInscnt();
  }
};

template <typename T>
bool aug_dfs(T *node,
	     T *target,
	     std::set<T*> &seen,
	     std::list<T*> &visited,
	     const std::set<T*> &init,
	     size_t curr_len, size_t max_len) {
  if(node == target) {
    visited.push_back(node);
    return true;
  }
  if(curr_len >= max_len) {
    return false;
  }

  auto it = seen.find(node);
  if(it != seen.end())
    return false;
  seen.insert(node);

  if(node->hasJAL() /*or node->hasJR() or node->hasJALR()*/) {
    bool viable = false;
    for(T *nn : node->getSuccs()) {
      if((nn == target) or (seen.find(nn) != seen.end())) {
	viable = true;
	break;
      }
    }
    if(not(viable))
      return false;
  }
  else if(node->hasJR() or node->hasJALR()) {
    return false;
  }
  else if(node->hasMONITOR()) {
    return false;
  }

  bool found_path = false;
  size_t num_succs = node->getSuccs().size();
  std::vector<T*> succs;
  succs.reserve(num_succs);
  for(T *n : node->getSuccs()) {
    succs.push_back(n);
  }
  
  std::sort(succs.begin(), succs.end(), sortByIcnt<T*>());
  
  for(T *n : succs) {
    if(aug_dfs(n, target, seen, visited, init, curr_len+1, max_len)) {
      visited.push_back(node);
      found_path = true;
    }
  }
  return found_path;
}


template <typename T>
void inducePhis(const std::set<cfgBasicBlock*> &defBBs, int id) {
  std::list<cfgBasicBlock*> workList;
  std::set<cfgBasicBlock*> checkSet;
  for(cfgBasicBlock* cbb : defBBs) { 
    workList.push_back(cbb); 
    checkSet.insert(cbb); 
  }
  while(not(workList.empty()))  {
    cfgBasicBlock *cbb = workList.front();
    workList.pop_front();
    for(cfgBasicBlock* dbb : cbb->dfrontier) {
      T* phi = new T(id);
      dbb->addPhiNode(phi);
      if(checkSet.find(dbb) == checkSet.end()) {
	checkSet.insert(dbb);
	workList.push_back(dbb);
      }
    }
  }
}

void regionCFG::dropCompiled() {
  while(not(regionCFGs.empty())) {
    auto bb = (*regionCFGs.begin())->head;
    assert(bb != nullptr);
    bb->dropCompiledCode();
  }
}
llvmRegTables::llvmRegTables(regionCFG *cfg) :
  MipsRegTable<llvm::Value>(),
  cfg(cfg),
  myIRBuilder(cfg->myIRBuilder),
  iCnt(nullptr) {}

llvmRegTables::llvmRegTables() :
  MipsRegTable<llvm::Value>(),
  cfg(nullptr),
  myIRBuilder(nullptr),
  iCnt(nullptr) {}

void llvmRegTables::copy(const llvmRegTables &other) {
  cfg = other.cfg;
  myIRBuilder = other.myIRBuilder;
  iCnt = other.iCnt;
  fprTbl = other.fprTbl;
  gprTbl = other.gprTbl;
  hiloTbl = other.hiloTbl;
  fcrTbl = other.fcrTbl;
}

llvm::Value *llvmRegTables::setGPR(uint32_t gpr, uint32_t x) {
  gprTbl[gpr] = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cfg->Context),x);
  return gprTbl[gpr];
}

llvm::Value *llvmRegTables::loadGPR(uint32_t gpr) {
  using namespace llvm;
  if(gprTbl[gpr]==nullptr)  {
    if(gpr==0) {
      gprTbl[0] = ConstantInt::get(Type::getInt32Ty(*cfg->Context),0);
    } 
    else {
      Value *offs = ConstantInt::get(Type::getInt32Ty(*cfg->Context),gpr);
      Value *gep = myIRBuilder->CreateGEP(cfg->blockArgMap["gpr"], offs);
      std::string ldName = getGPRName(gpr) + "_" + std::to_string(cfg->getuuid()++);
      Value *ld = myIRBuilder->CreateLoad(gep,ldName);
      gprTbl[gpr] = ld;
    }
  }
  return gprTbl[gpr];
}

llvm::Value *llvmRegTables::getFPR(uint32_t fpr, fprUseEnum useType) {
  assert(fprTbl[fpr]!=nullptr);
  if(cfg->allFprTouched[fpr]==fprUseEnum::singlePrec or
     cfg->allFprTouched[fpr]==fprUseEnum::doublePrec) {
    return fprTbl[fpr];
  }
  else if(cfg->allFprTouched[fpr]==fprUseEnum::both) {
    if(useType == fprUseEnum::singlePrec) {
      return fprTbl[fpr];
    }
    else if(useType == fprUseEnum::doublePrec) {
      assert((fpr&0x1) == 0);
      if(fprTbl[fpr+0] == nullptr) {
	loadFPR(fpr+0);
      }
      if(fprTbl[fpr+1] == nullptr) {
	loadFPR(fpr+1);
      }
      assert(fprTbl[fpr+0]);
      assert(fprTbl[fpr+1]);
      llvm::Value *vL = myIRBuilder->CreateBitCast(fprTbl[fpr+0], cfg->type_int32);
      llvm::Value *vH = myIRBuilder->CreateBitCast(fprTbl[fpr+1], cfg->type_int32);
      llvm::Value *vL64 = myIRBuilder->CreateZExt(vL, cfg->type_int64);
      llvm::Value *vH64 = myIRBuilder->CreateZExt(vH, cfg->type_int64);
      
      llvm::Value *v32 = llvm::ConstantInt::get(cfg->type_int64,32);
      vH64 = myIRBuilder->CreateShl(vH64, v32);
      llvm::Value *v = myIRBuilder->CreateOr(vH64, vL64);
      return myIRBuilder->CreateBitCast(v, cfg->type_double);
    }
    else {
      std::cerr << "f" << fpr << " : useType = " << useType << "\n";
      die();
    }
  }

  die();
  return nullptr;
}

void llvmRegTables::setFPR(uint32_t fpr, llvm::Value *v) {
  if(cfg->allFprTouched[fpr]==fprUseEnum::both) {
    if(v->getType() == cfg->type_float) {
      fprTbl[fpr] = v;
    }
    else if(v->getType() == cfg->type_double) {
      assert((fpr&0x1) == 0);
      v = myIRBuilder->CreateBitCast(v, cfg->type_int64);
      llvm::Value *v32 = llvm::ConstantInt::get(cfg->type_int64,32);
      llvm::Value *vShft = myIRBuilder->CreateLShr(v, v32);
      llvm::Value *vH = myIRBuilder->CreateTrunc(vShft, cfg->type_int32);
      llvm::Value *vL = myIRBuilder->CreateTrunc(v, cfg->type_int32);
      fprTbl[fpr+0] = myIRBuilder->CreateBitCast(vL, cfg->type_float);
      fprTbl[fpr+1] = myIRBuilder->CreateBitCast(vH, cfg->type_float);
    }
    else {
      die();
    }
  }
  else {
    fprTbl[fpr] = v;
  }
}


llvm::Value *llvmRegTables::loadFPR(uint32_t fpr) {
  if(fprTbl[fpr])
    return fprTbl[fpr];

  llvm::Value *offs = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cfg->Context),fpr);
  llvm::Value *basePtr = cfg->blockArgMap.at("cpr1");
  llvm::Value *gep = myIRBuilder->CreateGEP(basePtr, offs);
  dbt_assert(gep && "no gep!");

  if(cfg->allFprTouched[fpr]==fprUseEnum::singlePrec) {
    gep = myIRBuilder->CreatePointerCast(gep, llvm::Type::getFloatPtrTy(*cfg->Context));
  }
  else if(cfg->allFprTouched[fpr]==fprUseEnum::doublePrec) {
    gep = myIRBuilder->CreatePointerCast(gep, llvm::Type::getDoublePtrTy(*cfg->Context));
  }
  else {
    gep = myIRBuilder->CreatePointerCast(gep, llvm::Type::getFloatPtrTy(*cfg->Context));
  }
  std::string ldName = "f" + std::to_string(fpr) + "_" + std::to_string(cfg->getuuid()++);
  llvm::Value *ld = myIRBuilder->CreateLoad(gep,ldName);
  dbt_assert(ld && "no load");
  fprTbl[fpr] = ld;
  return fprTbl[fpr];
}

llvm::Value *llvmRegTables::loadHiLo(uint32_t h) {
  using namespace llvm;
  if(hiloTbl[h]==nullptr) {
    Value *vHiLoPtr=nullptr;
    Value *vZero = ConstantInt::get(Type::getInt32Ty(*cfg->Context),0);
    vHiLoPtr = myIRBuilder->CreateGEP(cfg->blockArgMap[h ? "hi" : "lo"], vZero);
    hiloTbl[h] = myIRBuilder->CreateLoad(vHiLoPtr);
  }
  return hiloTbl[h];
}
llvm::Value *llvmRegTables::loadFCR(uint32_t fcr) {
  if(fcrTbl[fcr]!=nullptr)
    return fcrTbl[fcr];

  llvm::Value *offs = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cfg->Context),fcr);
  llvm::Value *basePtr = cfg->blockArgMap.at("fcr1");
  llvm::Value *gep = myIRBuilder->CreateGEP(basePtr, offs);
  gep = myIRBuilder->CreatePointerCast(gep, llvm::Type::getInt32PtrTy(*cfg->Context));
  fcrTbl[fcr] = myIRBuilder->CreateLoad(gep);
  return fcrTbl[fcr];
}

void llvmRegTables::initIcnt() {
  llvm::Type *iType64 = llvm::Type::getInt64Ty(*(cfg->Context));
  llvm::Value *vZ = llvm::ConstantInt::get(iType64,0);
  llvm::Value *vG = myIRBuilder->CreateGEP(cfg->blockArgMap["icnt"], vZ);
  iCnt = myIRBuilder->CreateLoad(vG);
}

void llvmRegTables::incrIcnt(size_t amt) {
  llvm::Type *iType64 = llvm::Type::getInt64Ty(*(cfg->Context));
  llvm::Value *vAmt = llvm::ConstantInt::get(iType64,amt);
  iCnt = myIRBuilder->CreateAdd(iCnt, vAmt);
}
void llvmRegTables::storeIcnt() {
  using namespace llvm;
  Type *iType64 = Type::getInt64Ty(*(cfg->Context));
  Value *vZ = ConstantInt::get(iType64,0);
  Value *vG = myIRBuilder->CreateGEP(cfg->blockArgMap["icnt"], vZ);
  myIRBuilder->CreateStore(iCnt, vG);
}
void llvmRegTables::storeGPR(uint32_t gpr) {
  using namespace llvm;
  Value *offs = ConstantInt::get(Type::getInt32Ty(*(cfg->Context)),gpr);
  Value *gep = myIRBuilder->CreateGEP(cfg->blockArgMap["gpr"], offs);
  myIRBuilder->CreateStore(gprTbl[gpr], gep);
}
void llvmRegTables::storeFPR(uint32_t fpr) {
  using namespace llvm;
  Value *offs = ConstantInt::get(Type::getInt32Ty(*cfg->Context),fpr);
  Value *basePtr = cfg->blockArgMap.at("cpr1");
  Value *gep = myIRBuilder->CreateGEP(basePtr, offs);
  dbt_assert(gep && "no gep!");

  if(cfg->allFprTouched[fpr]==fprUseEnum::singlePrec) {
    gep = myIRBuilder->CreatePointerCast(gep,Type::getFloatPtrTy(*cfg->Context));
  }
  else if(cfg->allFprTouched[fpr]==fprUseEnum::doublePrec) {
    gep = myIRBuilder->CreatePointerCast(gep, Type::getDoublePtrTy(*cfg->Context));
  }
  else if(cfg->allFprTouched[fpr]==fprUseEnum::both) {
    gep = myIRBuilder->CreatePointerCast(gep,Type::getFloatPtrTy(*cfg->Context));
  }
  else {
    die();
  }
  myIRBuilder->CreateStore(fprTbl[fpr], gep);
}
void llvmRegTables::storeHiLo(uint32_t h) {
  using namespace llvm;
  Value *gep = nullptr;
  Value *vZero = ConstantInt::get(Type::getInt32Ty(*cfg->Context),0);
  gep = myIRBuilder->CreateGEP(cfg->blockArgMap[h ? "hi" : "lo"], vZero);
  myIRBuilder->CreateStore(hiloTbl[h], gep);
}
void llvmRegTables::storeFCR(uint32_t fcr) {
  using namespace llvm;
  Value *offs = ConstantInt::get(Type::getInt32Ty(*(cfg->Context)),fcr);
  Value *gep = myIRBuilder->CreateGEP(cfg->blockArgMap["fcr1"], offs);
  myIRBuilder->CreateStore(fcrTbl[fcr], gep);
}

void gprPhiNode::makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) {
  llvm::Type *iType32 = llvm::Type::getInt32Ty(*(cfg->Context));
  std::string phiName = getGPRName(gprId) + "_" + std::to_string(cfg->getuuid()++);
  regTbl.gprTbl[gprId] = lPhi = cfg->myIRBuilder->CreatePHI(iType32,0,phiName);
}
void hiloPhiNode::makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) {
  llvm::Type *iType32 = llvm::Type::getInt32Ty(*(cfg->Context));
  std::string phiName = std::string(hi ? "hi" : "lo") + "_" + std::to_string(cfg->getuuid()++);
  regTbl.hiloTbl[hi] = lPhi = cfg->myIRBuilder->CreatePHI(iType32,0,phiName);
}

void fprPhiNode::makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) {
  llvm::Type *myType = nullptr;
  if(cfg->allFprTouched[fprId]==fprUseEnum::singlePrec) {
    myType = llvm::Type::getFloatTy(*(cfg->Context));
  }
  else if(cfg->allFprTouched[fprId]==fprUseEnum::doublePrec) {
    myType = llvm::Type::getDoubleTy(*(cfg->Context));
  }
  else if(cfg->allFprTouched[fprId]==fprUseEnum::both) {
    myType = llvm::Type::getFloatTy(*(cfg->Context));
  }
  std::string phiName = "$" + std::to_string(fprId) + "_" + std::to_string(cfg->getuuid()++);
  lPhi = cfg->myIRBuilder->CreatePHI(myType,0,phiName);
  regTbl.setFPR(fprId,lPhi);

}
void fcrPhiNode::makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) {
  llvm::Type *iType32 = llvm::Type::getInt32Ty(*(cfg->Context));
  std::string fcrName =  "fcr_" + std::to_string(fcrId) + "_" + std::to_string(cfg->getuuid()++);
  regTbl.fcrTbl[fcrId] = lPhi = cfg->myIRBuilder->CreatePHI(iType32,0,fcrName); 
}

void icntPhiNode::makeLLVMPhi(regionCFG *cfg, llvmRegTables& regTbl) {
  llvm::Type *iType64 = llvm::Type::getInt64Ty(*(cfg->Context));
  std::string phiName =  "icnt_" + std::to_string(cfg->getuuid()++);
  regTbl.iCnt = lPhi = cfg->myIRBuilder->CreatePHI(iType64,0,phiName);
}


void regionCFG::getRegDefBlocks() {
  for(auto cbb : cfgBlocks) {
    for(size_t i = 0, n = cbb->insns.size(); i < n; i++) {
      Insn *ins = cbb->insns[i];
      ins->recDefines(cbb, this);
      ins->recUses(cbb);
    }
    /* Union bitvectors */
    allHiloRead[0] = allHiloRead[0] | cbb->hiloRead[0];
    for(size_t i = 0; i < 32; i++)
      allGprRead[i] = allGprRead[i] | cbb->gprRead[i];
    for(size_t i = 0; i < 32; i++)
      allFprRead[i] = allFprRead[i] | cbb->fprRead[i];
    for(size_t i = 0; i < 5; i++)
      allFcrRead[i] = allFcrRead[i] | cbb->fcrRead[i];

    const uint32_t mask = ~(0x1);
    for(size_t i = 0; i < 32; i++) {
      if(allFprTouched[i]==fprUseEnum::both || 
	 cbb->fprTouched[i] == fprUseEnum::unused) {
	continue;
      }
      else if(allFprTouched[i]==fprUseEnum::unused) {
	allFprTouched[i] = cbb->fprTouched[i];
      }
      else if(allFprTouched[i] != cbb->fprTouched[i]) { 
	//print_var(allFprTouched[i]);
	//print_var(cbb->fprTouched[i]);
	allFprTouched[(i&mask)+0] = fprUseEnum::both;
	allFprTouched[(i&mask)+1] = fprUseEnum::both;
      }
    }
  }
}


void regionCFG::emulate(state_t *ss){
  //basicBlock *c = head->findBlock(ss->pc);
  basicBlock *c = head->localFindBlock(ss->pc);
  dbt_assert(c->getEntryAddr() == ss->pc);
  while(c) {
    if(blocks.find(c) == blocks.end())
      break;
    c->run(ss);
    basicBlock *nb = c->localFindBlock(ss->pc);
    if(nb == nullptr) {
      globals::cBB = new basicBlock(ss->pc, c);
      break;
    }
    globals::cBB = c = nb;
  }
}

bool regionCFG::allBlocksReachable(cfgBasicBlock *root) {
  std::queue<cfgBasicBlock*> q;
  std::set<cfgBasicBlock*> v;
  std::list<cfgBasicBlock*> l;
  q.push(root);
  while(not(q.empty())) {
    cfgBasicBlock *cbb = q.front();
    q.pop();
    if(v.find(cbb) != v.end())
      continue;
    v.insert(cbb);
    l.push_back(cbb);
    for(auto nbb : cbb->succs) {
      q.push(nbb);
    }
  }
#if 0
  print_var_hex(root->getEntryAddr());
  print_var(l.size());
  for(auto &z : l) {
    std::cout << std::hex << z->getEntryAddr() << std::dec << " : "
	      << z->getSuccs().size() << "\n";
  }
#endif
  for(auto bb : cfgBlocks) {
    if(v.find(bb) == v.end()) {
      printf("couldn't find block %x:\n", bb->getEntryAddr());
    }
  }
  /*
  print_var(v.size());
  print_var(cfgBlocks.size());
  */
  return v.size() == cfgBlocks.size();
}


bool regionCFG::buildCFG(std::vector<std::vector<basicBlock*> > &regions) {
  compileTime = timestamp();
  std::set<basicBlock*> heads;
  std::map<basicBlock*, cfgBasicBlock*> cfgMap;
  std::vector<basicBlock*> blockvec;
  std::set<basicBlock*> discovered; 
  std::list<basicBlock*> visited;
  std::set<basicBlock*> seen;
  
  currCFG = this;

  for(size_t i = 0, n = regions.size(); i < n; i++) {
    heads.insert(regions[i][0]);
    for(size_t j = 0, nn=regions[i].size(); j < nn; j++) {
      basicBlock *bb = regions[i][j];
      if(bb==nullptr) die();
      blocks.insert(bb);
    }
  }
  
  if(heads.size()!=1) {
    printf("something has gone horribly wrong: %zu region heads in cfg\n", 
	   heads.size());
    die();
  }

  head = regions.at(0).at(0);
  if(head==nullptr) {
    die();
  }

  if(globals::verbose and false) {
    std::cerr << "regionCFG block @ 0x"
	      << std::hex << head->getEntryAddr()
	      << std::dec
	      << " with " << blocks.size()
	      << " basicblocks\n";
  }

  blockvec.reserve(blocks.size());
  for(auto bb : blocks) {
    blockvec.push_back(bb);
  }
  std::sort(blockvec.begin(), blockvec.end(), sortByIcnt<basicBlock*>());
  
  
  switch(globals::cfgAug)
    {
    case cfgAugEnum::none:
      break;
      /* find paths to the head bb */      
    case cfgAugEnum::head:
      for(auto bb: blockvec) {
	visited.clear();
	seen.clear();	
	if(aug_dfs<basicBlock>(head, bb, seen, visited, blocks, 0, 1024)) {
	  for(auto dbb : visited) {
	    discovered.insert(dbb);
	  }
	}
      }
      break;
      /* find paths to any initially discovered bb */
    case cfgAugEnum::aggressive:
      for(auto bb : blockvec) {
	for(auto nbb : blockvec) {
	  if(bb==nbb) continue;
	  visited.clear();
	  seen.clear();
	  if(aug_dfs<basicBlock>(nbb, bb, seen, visited, blocks, 0, 1024)) {
	    for(auto dbb : visited) {
	      discovered.insert(dbb);
	    }
	  }
	}
      }
      break;
    case cfgAugEnum::insane:
      discovered = blocks;
      for(auto sit = discovered.begin(); sit != discovered.end(); ++sit) {
	auto *bb = *sit;
	for(auto dit = discovered.begin(); dit != discovered.end(); ++dit) {
	  auto *nbb = *dit;
	  if(nbb==bb)
	    continue;
	  visited.clear();
	  seen.clear();		  
	  if(aug_dfs<basicBlock>(nbb, bb, seen, visited, blocks, 0, 1024)) {
	    for(auto dbb : visited) {
	      discovered.insert(dbb);
	    }
	  }
	}
      }
      break;
    }

  int added_blocks = 0;
  for(auto dbb : discovered) {
    if(blocks.find(dbb) != blocks.end()) {
      ++added_blocks;
    }
    blocks.insert(dbb);
  }
  //std::cout << "AUG FOUND " << added_blocks << "\n";
 
  std::list<basicBlock*> topoblocks;
  std::map<basicBlock*, double> regionProb;
  
  basicBlock::toposort(head, blocks, topoblocks);
  
  assert(*(topoblocks.begin()) == head);

  for(basicBlock* bb : topoblocks) {
    double pr = 1.0;
    if(bb != head) {
      pr = regionProb.at(bb);
    }
    for(basicBlock *nbb : bb->getSuccs()) {
      regionProb[nbb] += (pr * bb->edgeWeight(nbb->getEntryAddr()));
    }
  }
  headProb = regionProb.at(head);

  if(globals::splitCFGBBs) {
    for(auto bb : blocks) {
      bb->repairBrokenEdges();
    }
  }
  
  for(auto bb : blocks) {
    cfgBasicBlock *cbb = new cfgBasicBlock(bb);
    if(bb == head) {
      cfgHead = cbb;
    }
    cfgBlocks.push_back(cbb);
    cfgMap[bb] = cbb;
  }
  
  
  for(auto bb : blocks) {
    for(auto nbb : bb->getSuccs()) {
      auto it = cfgMap.find(nbb);
      if(it != cfgMap.end()) {
	cfgMap[bb]->addSuccessor(it->second);
      }
    }
  }

  if(globals::splitCFGBBs) {
    splitBBs();
  }
  
  /* implicit self loop when there's only one block */
  if(cfgBlocks.size()==1) {
    cfgMap[head]->addSuccessor(cfgMap[head]);
  }


  /* "compile" mips instructions into proper class */
  for(size_t i = 0; i < cfgBlocks.size(); i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    uint32_t ep = cbb->getEntryAddr();
    cbb->bindInsns(this);
    auto it = cfgBlockMap.find(ep);
    assert(it == cfgBlockMap.end());
    cfgBlockMap[ep] = cbb;
  }

  uint32_t typeCnts[dummyprec-integerprec] = {0};
  for(size_t i = 0; i < cfgBlocks.size(); i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    cbb->hasFloatingPoint(typeCnts);
  }

  //for(size_t i = 0; i < 5; i++) {
  //printf("%zu type %zu ins\n", typeCnts[i], i);
  //}

  for(size_t i = 0, nblks=cfgBlocks.size(); i < nblks; i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    if(not(cbb->canCompile())) {
      return false;
    }
  }

  /* Speculate that jr's can branch back to 
   * the region head */
  for(size_t i = 0; i < cfgBlocks.size(); i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    if(cbb->has_jr_jalr()) {
      cbb->addSuccessor(cfgMap[head]);
    }
  }

  /* find edges within cfg not found by 
   * tracing mechanism */
  for(size_t i = 0; i < cfgBlocks.size(); i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    cbb->addWithInCFGEdges(this);
  }

  for(size_t i = 0; i < cfgBlocks.size(); i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    if(!cbb->checkIfPlausableSuccessors()) {
      printf("found impossible edge\n");
      die();
    }
  }


  std::list<cfgBasicBlock*> likelyBlockList;
  for(size_t i = 0; i < cfgBlocks.size(); i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    //if(cbb->hasBranchLikely()) {
    if(cbb->haslikely()) {
      likelyBlockList.push_back(cbb);
    }
  }

  while(not(likelyBlockList.empty())) {
    cfgBasicBlock *cbb = likelyBlockList.front();
    likelyBlockList.pop_front();
    cfgBasicBlock *pbb = new cfgBasicBlock(cbb->bb, true);
    pbb->bindInsns(this);
    if(cbb->patchLikely(this, pbb)) {
      cfgBlocks.push_back(pbb);
    }
    else {
      delete pbb;
    }
  }
  if(not(allBlocksReachable(cfgMap[head]))) {
    die();
  }


  

  
  bool rc = analyzeGraph();
  if(not(rc) and globals::verbose) {
    std::cout << "COMPILE FAILED  in analysis\n";
  }
  if(rc) {
    generateMachineCode(globals::regionOptLevel);
    compileTime = timestamp() - compileTime;
  }
  return rc;
}

bool regionCFG::analyzeGraph() {
  entryBlock = new cfgBasicBlock(nullptr, false);
  cfgBlocks.push_back(entryBlock);
  entryBlock->addSuccessor(cfgHead);
  globals::nCfgCompiles++;
  
  if(cfgBlocks.size() < 512) {
    computeDominance();
  }
  else {
    computeLengauerTarjanDominance();
  }

  fastDominancePreComputation();
  
  computeDominanceFrontiers();
  /* search for natural loops */
  findNaturalLoops();
  
  /* "compile" mips instructions into proper class */
  for(size_t i = 0, n = cfgBlocks.size(); i < n; i++) {
    cfgBasicBlock *cbb = cfgBlocks[i];
    cbb->bindInsns(this);
  }

  /* insert phis into basicblocks */
  insertPhis();


  for(size_t i = 0, nr = allFprTouched.size(); i < nr; i++) {
    fprUseEnum useType = allFprTouched.at(i);
    for(cfgBasicBlock *cbb : cfgBlocks) {
      if(cbb->fprPhis[i]) {
	auto fPhi = dynamic_cast<fprPhiNode*>(cbb->fprPhis[i]);
	assert(fPhi);
	fPhi->setUseType(useType);
      }
    }
    hasBoth |= useType == fprUseEnum::both;
    if(useType == fprUseEnum::both and not(globals::enableBoth)) {
      return false;
    }
  }
    
  uint32_t cnt = 0;
  bool usesFCR = false, usesFPR = false;
  for(size_t i = 0; i < 32; i++)
    cnt = allGprRead[i] ? cnt + 1 : cnt;
  cnt = allHiloRead[0] ? cnt + 1 : cnt;

  for(size_t i = 0; i < 32; i++) {
    cnt = allFprRead[i] ? cnt + 1 : cnt;
    usesFPR |= (allFprRead[i]!=0);
  }
  
  for(size_t i = 0; i < 5; i++) {
    cnt = allFcrRead[i] ? cnt + 1 : cnt;
    usesFCR |= (allFcrRead[i]!=0);
  }

  initLLVMAndGeneratePreamble();
  entryBlock->traverseAndRename(this);
  entryBlock->patchUpPhiNodes(this);

  
  std::string _errors;
  llvm::raw_string_ostream OS(_errors);
  if(llvm::verifyModule(*myModule, &OS)) {
    std::cerr << _errors;
    llvm::errs() << *myModule;
    dumpIR();
    dumpLLVM();
    die();
  }

  if(globals::dumpIR) {
    dumpIR();
    dumpLLVM();
  }

  return true;
}

void regionCFG::initLLVMAndGeneratePreamble() {
  std::vector<llvm::Type*> blockArgTypes;
  llvm::FunctionType *blockFunctionType = 0;
  std::vector<std::string> blockArgNames;

  std::string tempName = "cfg_" + toStringHex(head->getEntryAddr());
  std::string modName = tempName + "_module";
  std::string entryName = tempName + "_entry";

#if (LLVM_VERSION_MAJOR==3 && LLVM_VERSION_MINOR < 9)
  Context = &(llvm::getGlobalContext());
#else
  Context = &globalContext;
#endif
  myIRBuilder = new llvm::IRBuilder<>(*Context);
  myModule = new llvm::Module(modName, *Context);
  type_iPtr32 = llvm::Type::getInt32PtrTy(*Context);
  type_void = llvm::Type::getVoidTy(*Context);
  type_iPtr8 = llvm::Type::getInt8PtrTy(*Context);
  type_iPtr64 = llvm::Type::getInt64PtrTy(*Context);
  type_int32 = llvm::Type::getInt32Ty(*Context);
  type_int64 = llvm::Type::getInt64Ty(*Context);
  type_double = llvm::Type::getDoubleTy(*Context);
  type_float = llvm::Type::getFloatTy(*Context);


  /* not sure why this doesn't work on LLVM > 4 (TODO FIX) */
#if (LLVM_VERSION_MAJOR<5)
  llvm::Constant *cFunc = nullptr;
  cFunc = myModule->getOrInsertFunction("print_double", type_void, type_double,  nullptr);
  builtinFuncts["print_double"] = llvm::cast<llvm::Function>(cFunc);
  cFunc = myModule->getOrInsertFunction("print_double2", type_void, type_double, type_double, nullptr);
  builtinFuncts["print_double2"] = llvm::cast<llvm::Function>(cFunc);
  cFunc = myModule->getOrInsertFunction("print_float", type_void, type_float, nullptr);
  builtinFuncts["print_float"] = llvm::cast<llvm::Function>(cFunc);
  cFunc = myModule->getOrInsertFunction("print_float2", type_void, type_float, type_float, nullptr);
  builtinFuncts["print_float2"] = llvm::cast<llvm::Function>(cFunc);
  cFunc = myModule->getOrInsertFunction("print_int32", type_void, type_int32,  nullptr);
  builtinFuncts["print_int32"] = llvm::cast<llvm::Function>(cFunc);
  cFunc = myModule->getOrInsertFunction("log_bb", type_void, type_int32, type_int64,  nullptr);
  builtinFuncts["log_bb"] = llvm::cast<llvm::Function>(cFunc);
#endif
  
  blockArgTypes.push_back(type_iPtr32);
  blockArgNames.push_back("pc");
  blockArgTypes.push_back(type_iPtr32);
  blockArgNames.push_back("gpr");
  blockArgTypes.push_back(type_iPtr32);
  blockArgNames.push_back("cpr0");
  blockArgTypes.push_back(type_iPtr32);
  blockArgNames.push_back("cpr1");
  blockArgTypes.push_back(type_iPtr32);
  blockArgNames.push_back("fcr1");
  blockArgTypes.push_back(type_iPtr8);
  blockArgNames.push_back("mem");
  blockArgTypes.push_back(type_iPtr32);
  blockArgNames.push_back("lo");
  blockArgTypes.push_back(type_iPtr32);
  blockArgNames.push_back("hi");
  blockArgTypes.push_back(type_iPtr64);
  blockArgNames.push_back("icnt");
  blockArgTypes.push_back(type_iPtr64);
  blockArgNames.push_back("abortloc");
  blockArgTypes.push_back(type_iPtr64);
  blockArgNames.push_back("nextbb");

  llvm::ArrayRef<llvm::Type*> blockArgs(blockArgTypes);
  blockFunctionType = llvm::FunctionType::get(type_void,blockArgs,false);
  blockFunction = llvm::Function::Create(blockFunctionType, 
					 llvm::Function::ExternalLinkage,
					 tempName, 
					 myModule);
  size_t idx = 0;
  for (auto AI = blockFunction->arg_begin(), E = blockFunction->arg_end();
       AI != E; ++AI) {
    AI->setName(blockArgNames[idx]);
    blockArgMap[blockArgNames[idx]] = &(*AI);
    idx++;
  }

  /* first defined block must be entry */
  entryBlock->lBB = llvm::BasicBlock::Create(*Context,tempName + "_ENTRY",blockFunction);

  for(size_t i = 0; i < cfgBlocks.size(); i++) {
    if(cfgBlocks[i] == entryBlock)
      continue;
    
    std::string blockName = toStringHex(cfgBlocks[i]->getEntryAddr());

    cfgBlocks[i]->lBB = llvm::BasicBlock::Create(*Context,blockName,blockFunction);
  }

}

regionCFG::regionCFG() : execUnit() {
  regionCFGs.insert(this);
  perfectNest = true;
  isMegaRegion = false;
  innerPerfectBlock = 0;
  pmap = perfmap::getReference();
  runs = uuid = 0;
  minIcnt = std::numeric_limits<uint64_t>::max();
  maxIcnt = 0; 
  codeBits = nullptr;
  headProb = 0.0;
  head = nullptr;
  cfgHead = nullptr;
  entryBlock = 0;
  Context = 0;
  blockFunction = 0;
  hasBoth = false;
  validDominanceAcceleration = false;
  compileTime = 0.0;
  myIRBuilder=nullptr;
  myModule= nullptr;
  myEngineBuilder=nullptr;
  myExecEngine=nullptr;
  allFprTouched.resize(32, fprUseEnum::unused);
  runHistory.fill(0);
}
regionCFG::~regionCFG() {
  regionCFGs.erase(regionCFGs.find(this));
  pmap->relReference();
  
  for(auto cblk : cfgBlocks) {
    delete cblk;
  }
  cfgBlocks.clear();
    
  if(myIRBuilder)
    delete myIRBuilder; 

  if(myExecEngine)
    delete myExecEngine;

  if(myEngineBuilder)
    delete myEngineBuilder;
}

 
void regionCFG::insertPhis()  {
  /* find blocks where registers are "defined" */
  getRegDefBlocks();
  /* if any register is written in the cfg */
  for(size_t gpr = 0; gpr < 32; gpr++) {
    if(!gprDefinitionBlocks[gpr].empty())
      gprDefinitionBlocks[gpr].insert(entryBlock);
  }
  if(!hiloDefinitionBlocks.empty())
    hiloDefinitionBlocks.insert(entryBlock);
  for(size_t fpr = 0; fpr < 32; fpr++) {
    if(!fprDefinitionBlocks[fpr].empty())
      fprDefinitionBlocks[fpr].insert(entryBlock);
  }
  for(size_t cpr = 0; cpr < 5; cpr++) {
    if(!fcrDefinitionBlocks[cpr].empty())
      fcrDefinitionBlocks[cpr].insert(entryBlock);
  }
  /* handle gprs */
  for(size_t gpr = 1; gpr < 32; gpr++) {
    inducePhis<gprPhiNode>(gprDefinitionBlocks[gpr], gpr);
  }
  /* handle lo-hi registers */
  for(size_t hl = 0; hl < 2; hl++) {
    inducePhis<hiloPhiNode>(hiloDefinitionBlocks, hl);
  }
  /* handle fprs */
  for(size_t fpr = 0; fpr < 32; fpr+=2)  {
    if(allFprTouched[fpr] == fprUseEnum::both) {
      std::set<cfgBasicBlock*> unionedDefs;
      for(auto bb : fprDefinitionBlocks[fpr]) {
	unionedDefs.insert(bb);
      }
      for(auto bb : fprDefinitionBlocks[fpr+1]) {
	unionedDefs.insert(bb);
      }
      fprDefinitionBlocks[fpr] = unionedDefs;
      fprDefinitionBlocks[fpr+1] = unionedDefs;
    }
  }
  
  for(size_t fpr = 0; fpr < 32; fpr++)  {
    inducePhis<fprPhiNode>(fprDefinitionBlocks[fpr], fpr);
  }

  
  
  /* handle cprs */
  for(size_t fcr = 0; fcr < 5; fcr++) {
    inducePhis<fcrPhiNode>(fcrDefinitionBlocks[fcr], fcr);
  }
  /* handle icnt */
  if(globals::countInsns) {
    std::set<cfgBasicBlock*> allBlocks;
    for(auto bb : cfgBlocks) {
      allBlocks.insert(bb);
    }
    inducePhis<icntPhiNode>(allBlocks, 0);
  }
}

void regionCFG::fastDominancePreComputation() {
  using namespace std;
  size_t cnt = 0;
  /* run DFS on the dominator tree */
  function<void(cfgBasicBlock*)> dfs =[&](cfgBasicBlock* bb) {
    cnt++;
    /* pre-order - get dfnum */
    bb->dt_dfn = cnt;
    for(auto nbb : bb->getDTSuccs()) {
      /* dom-tree .. no cycles */
      dfs(nbb);
    }
    /* post-order - max dfnum of successors */
    bb->dt_max_ancestor_dfn = cnt;
  };
  dfs(entryBlock);

  validDominanceAcceleration = true;

#if 0
  for(size_t i = 0, n = cfgBlocks.size(); i < n; i++) {
    auto A = cfgBlocks.at(i);
    for(size_t j = i; j < n; j++) {
      auto B = cfgBlocks.at(j);
      bool d0 = A->fastDominates(B);
      bool d1 = A->dominates(B);
      assert(d0 == d1);
    }
  }
#endif
}

void regionCFG::computeLengauerTarjanDominance() {
  LengauerTarjanDominators LTD(cfgBlocks);
  LTD();
  
}
 
void regionCFG::computeDominance() {
  using namespace boost;
  using namespace std;
  bool changed = true;
  vector<cfgBasicBlock*> preOrderVisit(cfgBlocks.size());
  std::map<cfgBasicBlock*, size_t> blockToNumMap;
  
  map<cfgBasicBlock*, dynamic_bitset<>> domMap;
  
  size_t dfsNum = 0;

  /* use DFS to compute DFS numbers */
  function<void(cfgBasicBlock*)> dfs =[&](cfgBasicBlock* bb) {
    preOrderVisit.at(dfsNum) = bb;
    blockToNumMap[bb] = dfsNum;
    dfsNum++;
    domMap[bb] = dynamic_bitset<>(cfgBlocks.size());
    domMap.at(bb).set();
    for(auto nbb : bb->succs) {
      auto it = blockToNumMap.find(nbb);
      if(it == blockToNumMap.end()) {
	dfs(nbb);
      }
    }
  };
  
  /* initialize */
  dfs(entryBlock);

  domMap.at(entryBlock).reset();
  domMap.at(entryBlock)[0] = true;

  
  /* compute dominators */
  do {
    changed = false;
    for(size_t bId=0, n=preOrderVisit.size(); bId < n; bId++) {
      cfgBasicBlock *cbb = preOrderVisit[bId];
      dynamic_bitset<> tdd = domMap.at(cbb);
      for(cfgBasicBlock *nbb : cbb->getPreds()) {
	tdd &= domMap.at(nbb);
      }
      tdd[bId] = true;
      //check if changed
      if(tdd != domMap.at(cbb)) {
	changed = true;
	domMap.at(cbb) = tdd;
      }
    }
  }
  while(changed);
  
  for(size_t i = 1, n = preOrderVisit.size(); i < n; i++) {
    cfgBasicBlock *cbb = preOrderVisit[i];
    dynamic_bitset<> &dom = domMap.at(cbb);
    dom[i] = false;
    /* iterate over blocks that dominate current block */
    for(size_t j = dom.find_first(); j != dynamic_bitset<>::npos; j = dom.find_next(j)) {
      const dynamic_bitset<> &ddom = domMap.at(preOrderVisit.at(j));
      for(size_t k = ddom.find_first(); k != dynamic_bitset<>::npos; k = ddom.find_next(k)) {
	/* if j dominates k, k can not be the immediate dominator */
	if(k!=j)
	  dom[k]=false;
      }
    }
    cbb->getIdom() = preOrderVisit.at(dom.find_first());
    cbb->getIdom()->addDTreeSucc(cbb);
  }
  entryBlock->getIdom() = nullptr;
}
 
void regionCFG::computeDominanceFrontiers() {
  /* compute dominance frontiers */
  for(auto cbb : cfgBlocks) {
    /* only blocks with more than one pred
     * can update dominance frontiers */
    if(cbb->preds.size() > 1) {
      for(auto nbb : cbb->getPreds()) {
	/* iterate back to idom 
	 * adding this node to 
	 * appropriate dominance frontiers */
	while((nbb != cbb->getIdom())) {
	    nbb->dfrontier.insert(cbb);
	    nbb = nbb->getIdom();
	  }
      }
    }
  }
}

bool regionCFG::dominates(cfgBasicBlock *A, cfgBasicBlock *B) const {
  if(validDominanceAcceleration) {
    return A->fastDominates(B);
  }
  else {
    return A->dominates(B);
  }
}



bool phiNode::parentInLLVM(cfgBasicBlock *b) {
  llvm::BasicBlock *BB = lPhi->getParent();
  for (llvm::pred_iterator PI = llvm::pred_begin(BB), 
	 E = llvm::pred_end(BB); PI != E; ++PI) {
    llvm::BasicBlock *Pred = *PI;
    if(b->lBB == Pred) {
      return true;
    }
  }
  return false;
}

llvm::BasicBlock *phiNode::getLLVMParentBlock(cfgBasicBlock *b) {
  llvm::BasicBlock *BB = lPhi->getParent();
  llvm::BasicBlock *lbb = b->lBB;
  if(!parentInLLVM(b) && b->has_jr_jalr() ) {
    lbb = b->jrMap[BB];
  }
  return lbb;
}

void gprPhiNode::addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) {
  if(!b) {
    printf("NO BLOCK\n");
    exit(-1);
  }
  llvm::Value *v = b->termRegTbl.gprTbl[gprId];
  if(!v) {
    std::string bname = b->bb ? toStringHex(b->bb->getEntryAddr()) : "ENTRY";
    printf("missing value for %s from %s!!!\n", 
	   getGPRName(gprId).c_str(), bname.c_str());
    cfg->asDot();
    die();
  }
  lPhi->addIncoming(v,getLLVMParentBlock(b));
}
void hiloPhiNode::addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) {
  llvm::Value *vHiLo = b->termRegTbl.hiloTbl[hi];
  if(!vHiLo) {
    printf("missing value for %s!\n", hi ? "hi" : "lo");
    die();
  }
  if(!lPhi) {
    die();
  }
  lPhi->addIncoming(vHiLo,getLLVMParentBlock(b));
}
void fprPhiNode::addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) {
  dbt_assert(b); 

  llvm::Value *v = nullptr;
  if(cfg->allFprTouched[fprId] == fprUseEnum::both) {
    v = b->termRegTbl.getFPR(fprId, fprUseEnum::singlePrec);
  }
  else {
    v = b->termRegTbl.getFPR(fprId, cfg->allFprTouched[fprId]);
  }
  ///llvm::errs() << "adding edge for fpr : " << fprId
  //<< "  : " << *v << "\n";

  dbt_assert(v);
  lPhi->addIncoming(v,getLLVMParentBlock(b));
}
void fcrPhiNode::addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) {
  dbt_assert(b); 
  llvm::Value *v = b->termRegTbl.fcrTbl[fcrId];
  dbt_assert(v);
  lPhi->addIncoming(v,getLLVMParentBlock(b));
}
void icntPhiNode::addIncomingEdge(regionCFG *cfg, cfgBasicBlock *b) {
  llvm::Value *vICnt = b->termRegTbl.iCnt;
  lPhi->addIncoming(vICnt,getLLVMParentBlock(b));
}


void regionCFG::asDot() const {
  const std::string filename = "cfg_" + toStringHex(head->getEntryAddr()) + ".dot"; 
  std::ofstream out(filename);
  std::set<const basicBlock*> bbs;
  
  for(const cfgBasicBlock* cbb : cfgBlocks) {
    const basicBlock *bb = cbb->bb;
    if(bb) {
      bbs.insert(bb);
    }
  }
  
  out << "digraph G {\n";
  /* vertices */
  
  for(const auto bb : bbs) {
    const auto & insns = bb->getVecIns();
    uint32_t ea = bb->getEntryAddr();
    out << "\"bb" << std::hex << ea << std::dec << "\"[\n";
    out << "label = <bb_0x" << std::hex << ea << std::dec
	<< " : " << "<BR align='left'/>";
    for(ssize_t i = 0, ni = insns.size(); i < ni; i++) {
      const auto &p = insns.at(i);
      uint32_t inst = p.first, addr = i*4 + ea;
      auto asmString = getAsmString(inst, addr);
      out << std::hex << addr << std::dec
	  << " : " << asmString << "<BR align='left'/>";
     
    }
    out << ">\nshape=\"record\"\n];\n";
  }
  /* edges */
  for(const auto* bb : bbs) {
    std::stringstream ss;
    ss << "\"bb" << std::hex << bb->getEntryAddr() << std::dec << "\"";
    std::string s = ss.str();
    for(const auto &nbb : bb->getSuccs()) {
      out << s
	  << " -> "
	  << "\"bb"
	  << std::hex
	  << nbb->getEntryAddr()
	  << std::dec
	  << "\"\n"; 
    }
  }
  
  out << "}\n";
  out.close();
}


llvm::BasicBlock* regionCFG::generateAbortBasicBlock(uint32_t abortpc, 
						    llvmRegTables& regTbl,
						    cfgBasicBlock *cBB,
						    llvm::BasicBlock *lBB,
						    uint32_t locpc) {
#if 0
  if(lBB == nullptr) {
    for(const auto &bb : blocks) {
      uint32_t e = bb->getEntryAddr();
      for(size_t i = 0, n = bb->getNumIns(); i < n; ++i) {
	uint32_t a = e + (4*i);
	if(a == abortpc) {
	  std::cerr << "ABORTING TO PC "
		    << std::hex
		    << abortpc
		    << " from "
		    << locpc
		    << std::dec
		    << " IN TRACE!!!\n";

	  basicBlock *sbb = nullptr;
	  for(auto &zbb : blocks) {
	    uint32_t sa = zbb->getEntryAddr();
	    uint32_t ea = sa + (4*zbb->getNumIns());
	    if(locpc >= sa and locpc < ea) {
	      sbb = zbb;
	      break;
	    }
	  }
	  if(sbb) {
	    std::cerr << "source:\n";
	    std::cerr << *sbb;
	  }
	  std::cerr << "target:\n";
	  std::cerr << *bb;
	  //dbt_backregion();
	  //abort();
	}
      }
    }
  }
#endif
  
  llvm::Value *vNPC = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*Context),abortpc);
  return generateAbortBasicBlock(vNPC,regTbl,cBB,lBB);
}

llvm::BasicBlock* regionCFG::generateAbortBasicBlock(llvm::Value *abortpc, 
						    llvmRegTables& regTbl,
						    cfgBasicBlock *cBB,
						    llvm::BasicBlock *lBB) {
  std::string abortName = "ABORT_" + std::to_string(uuid++);

  if(lBB)
    return lBB;

  llvm::BasicBlock *saveBB = myIRBuilder->GetInsertBlock();
  llvm::BasicBlock *abortBB = llvm::BasicBlock::Create(*Context,abortName,
						       blockFunction);
 
  myIRBuilder->SetInsertPoint(abortBB);

  //flush PC
  llvm::Value *offs = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*Context),0);
  llvm::Value *gep = myIRBuilder->CreateGEP(blockArgMap["pc"], offs);
  llvm::Value *vPtr = myIRBuilder->CreateBitCast(gep, llvm::Type::getInt32PtrTy(*Context));
  myIRBuilder->CreateStore(abortpc,vPtr);

  llvm::Value *vNPC = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*Context),(uint64_t)(cBB->bb));
  gep = myIRBuilder->CreateGEP(blockArgMap["abortloc"], offs);
  vPtr = myIRBuilder->CreateBitCast(gep, llvm::Type::getInt64PtrTy(*Context));
  myIRBuilder->CreateStore(vNPC,vPtr);

  /* we can't statically determine the next basic block */
  llvm::Value *vNBB = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*Context),(uint64_t)nullptr);
  gep = myIRBuilder->CreateGEP(blockArgMap["nextbb"], offs);
  vPtr = myIRBuilder->CreateBitCast(gep, llvm::Type::getInt64PtrTy(*Context));
  myIRBuilder->CreateStore(vNBB,vPtr);



  for(size_t i = 0; i < 32; i++) {
    if(!gprDefinitionBlocks[i].empty())
      regTbl.storeGPR(i);
  }
  
  if(!hiloDefinitionBlocks.empty()) {
    regTbl.storeHiLo(0);
    regTbl.storeHiLo(1);
  }

  for(size_t i = 0; i < 32; i++) {
    if(!fprDefinitionBlocks[i].empty())
      regTbl.storeFPR(i);
  }
  
  for(size_t i = 0; i < 5; i++) {
    if(!fcrDefinitionBlocks[i].empty())
      regTbl.storeFCR(i);
  }

  if(globals::countInsns) {
    regTbl.storeIcnt();
  }
  
  myIRBuilder->CreateRetVoid();  
  myIRBuilder->SetInsertPoint(saveBB);
  return abortBB;
}







void regionCFG::generateMachineCode( llvm::CodeGenOpt::Level optLevel){
  std::string errStr;
  myEngineBuilder = new llvm::EngineBuilder(std::unique_ptr<llvm::Module>(myModule));
  myEngineBuilder->setOptLevel(optLevel);
  myEngineBuilder->setErrorStr(&errStr);
#ifdef __amd64_
  myEngineBuilder->setMCPU("corei7");
#endif
  myExecEngine = myEngineBuilder->create();

#ifdef USE_VTUNE
  llvm::JITEventListener *vtuneProfiler = 
    llvm::JITEventListener::createIntelJITEventListener();
  myExecEngine->RegisterJITEventListener(vtuneProfiler);
#endif
  myExecEngine->finalizeObject();
  codeBits = (compiledCFG)myExecEngine->getPointerToFunction(blockFunction);
  std::string headname = "cfg_";
  if(perfectNest) {
    headname += "perfectNest_";
  }
  headname += toStringHex(cfgHead->getEntryAddr());
  pmap->addEntry((uint64_t)codeBits, 1<<12, headname);
}

std::ostream &operator<<(std::ostream &out, const regionCFG &cfg) {
  std::vector<cfgBasicBlock*> topo;
  cfg.toposort(topo);
  for(size_t i = 0, n = topo.size(); i < n; i++) {
    const cfgBasicBlock *bb = topo.at(i);
    out << "block 0x" << std::hex << bb->getEntryAddr()
	<< std::dec << " : ";
    for(const cfgBasicBlock *nbb : bb->getSuccs()) {
      out << std::hex << nbb->getEntryAddr()
	  << std::dec << " ";
    }
    out << "\n";
    const auto &c = bb->rawInsns;
    for(size_t j = 0, nb = c.size(); j < nb; j++) {
      uint32_t inst = c.at(j).first;
      uint32_t addr = c.at(j).second;
      out << "\t" << std::hex << addr << std::dec << " : ";      
      disassemble(out,inst,addr);
      out << "\n";
    }
  }
  return out;
}

void regionCFG::print() {
  std::cerr << *this << std::endl;
}

basicBlock* regionCFG::run(state_t *ss) {
  globals::currUnit = this;
  uint64_t nextbb = 0;
  uint64_t i0=ss->icnt;
  codeBits(
	   &(ss->pc), 
	   ss->gpr,
	   ss->cpr0, 
	   ss->cpr1,
	   ss->fcr1,
	   ss->mem,
	   &(ss->lo),
	   &(ss->hi),
	   &(ss->icnt),
	   &(ss->abortloc),
	   &nextbb
	   );
  globals::cBB = reinterpret_cast<basicBlock*>(ss->abortloc);
  i0 = ss->icnt - i0;
  minIcnt = std::min(minIcnt, i0);
  maxIcnt = std::max(maxIcnt, i0);
  runHistory[runs%histoLen] = i0;
  runs++;
  
  inscnt+=i0;
  icnt +=i0;
  iters++;
  
  if(nextbb==0) {
    //return globals::cBB->findBlock(ss->pc);
    return globals::cBB->globalFindBlock(ss->pc);
  }
  else {
    basicBlock *rbb = reinterpret_cast<basicBlock*>(nextbb);
    return rbb;
  }
}

void regionCFG::dumpIR() {
   std::string o_name= "cfg_" + toStringHex(cfgHead->getEntryAddr()) + ".txt";
   std::ofstream o(o_name.c_str());
   o << *this;
   o.close();
 }
 
void regionCFG::dumpLLVM() {
  std::string bitname= "cfg_" + toStringHex(cfgHead->getEntryAddr()) + ".bc"; 
  int fd = open(bitname.c_str(), O_RDWR|O_CREAT, (S_IRUSR | S_IWUSR) );
  llvm::raw_fd_ostream bcOut(fd, false, false);
  llvm::WriteBitcodeToFile(*myModule, bcOut);
  bcOut.close();
  close(fd);
}


void regionCFG::runLLVMLoopAnalysis() {}


void regionCFG::findLoop(std::set<cfgBasicBlock*> &loop, 
			std::list<cfgBasicBlock*> &stack,
			cfgBasicBlock *hbb) {
  while(!stack.empty()) {
    cfgBasicBlock *c = stack.front();
    stack.pop_front();
    loop.insert(c);
    if(c != hbb) {
      for(std::set<cfgBasicBlock*>::iterator sit = c->preds.begin(); sit != c->preds.end(); sit++) {
	cfgBasicBlock *cc = *sit;
	if(loop.find(cc) == loop.end()) {	
	  stack.push_front(cc);
	}
      }
    }
  }
}


void regionCFG::findNaturalLoops() {
  std::vector<naturalLoop> loops;
  std::map<cfgBasicBlock*, std::vector<naturalLoop> >loopHeadMap;
  for(cfgBasicBlock *lbb : cfgBlocks) {
    for(cfgBasicBlock *hbb : lbb->succs) {
      if(dominates(hbb, lbb)) {
	std::set<cfgBasicBlock*> loop;
	std::list<cfgBasicBlock*> stack;
	stack.push_front(lbb);
	/* BFS on preds to find natural loops */
	findLoop(loop, stack, hbb);
	naturalLoop l(hbb,loop);
	//loops.push_back(l);
	loopHeadMap[hbb].push_back(l);
      }
    }
  }

  for(std::map<cfgBasicBlock*, std::vector<naturalLoop> >::iterator mit = loopHeadMap.begin();
      mit != loopHeadMap.end(); mit++) {
    cfgBasicBlock *hbb = mit->first;
    std::set<cfgBasicBlock*> loop;
    if(mit->second.size() > 1)
      perfectNest = false;
    for(std::vector<naturalLoop>::iterator vit = mit->second.begin(); vit != mit->second.end(); vit++) {
      for(auto cfgbb : vit->getLoop()) {
	loop.insert(cfgbb);
      }
    }
    naturalLoop l(hbb,loop);
    loops.push_back(l);
  }
  if(loops.empty())
    return;
  
  std::sort(loops.begin(), loops.end(), sortNaturalLoops());

  std::vector<std::vector<size_t> > nests(loops.size());
  std::vector<int> depths(loops.size());
  std::fill(depths.begin(), depths.end(), 0);

  /* Building subset matrix */
  for(ssize_t i = loops.size()-1; i >= 0; i--) {
    for(ssize_t j = i - 1; j >= 0; j--) {
      /* Is loop i nested in loop(nest) j? */
      if(loops[i].isNestedLoop(loops[j])){
	nests[j].push_back(i);
	break;
      }
    }
  }

  //for(size_t i = 0; i < nests.size(); i++) {
  //printf("nest %zu: ", i);
  // for(size_t j = 0; j < nests[i].size(); j++) {
  //  printf("%zu ", nests[i][j]);
  // }
  // printf("\n");
  //}
  
  /* Traverse nesting tree using BFS 
   * to find max depth */
  std::queue<std::pair<size_t, int> > q;
  q.push(std::pair<size_t,int>(0,1));
  int maxDepth = 1;
  while(!q.empty()) {
    std::pair<size_t, int> p= q.front();
    /* assign loop to proper depth */
    depths[p.first] = p.second;
    q.pop();
    for(size_t i = 0; i < nests[p.first].size(); i++) {
      int d = p.second+1;
      maxDepth = std::max(maxDepth, d);
      std::pair<size_t,int> pp(nests[p.first][i], d);
      q.push(pp);
    }
  }

  
  //for(size_t i = 0; i < nests.size(); i++) {
  // printf("nest (depth=%d) %zu: ", i, depths[i]);
  //for(size_t j = 0; j < nests[i].size(); j++) {
  //  printf("%zu ", nests[i][j]);
  // }
  // printf("\n");
  //}



  /*printf("maxDepth = %d\n", maxDepth); */
  std::vector< std::vector<naturalLoop> > loopN(maxDepth+1);
  
  for(size_t i = 0; i < loops.size(); i++) {
    int d = depths[i];
    loopN[d].push_back(loops[i]);
  }
  

  for(size_t i = 1; i < loopN.size(); i++) {
    perfectNest &= (loopN[i].size()==1);
  }

  //for(size_t i = 1; i < loopN.size(); i++) {
  // printf("%zu\n", loopN[i].size());
  // for(size_t j = 0; j < loopN[i].size(); j++) {
  //   printf("depth %zu : ", i); loopN[i][j].print();
  // }
  //}

  //printf("head %x %s a perfect nest\n", 
  //head->getEntryAddr(), perfectNest ? "is" : "isnt" );
    
  loopNesting = loopN;
}

void regionCFG::report(std::string &s, uint64_t icnt) {
  double frac = ((double)inscnt / (double)icnt)*100.0;
  std::stringstream ss;
  ss << "compilation region @ 0x" << std::hex << head->getEntryAddr() << std::dec
     << "(inscnt=" << inscnt
     << ",head prob=" << headProb
     << ",min icnt=" << minIcnt
     << ",max icnt=" << maxIcnt
     << ",avg insns=" << (static_cast<double>(inscnt) / runs)
     << ",nextpcs = " << nextPCs.size()
     << ",static icnt = " << countInsns()
     << ",compile time = " << compileTime
     << ",frac=" << frac << ")\n";
  s += ss.str();
#if 1
  if(frac > 0.10) {
    dumpIR();
    dumpLLVM();
    asDot();
  }
#endif
  for(const auto & blk : cfgBlocks) {
    uint32_t x = blk->getEntryAddr();
    if(x != ~(0U)) {
      s += toStringHex(x) + ",";
      debugSymDB::lookup(blk->getEntryAddr(),s);
    }
  }
  s += "\n\n";
}
 
uint32_t regionCFG::getEntryAddr() const {
  return head ? head->getEntryAddr() : ~0;
}

void regionCFG::info() {
  std::cout << __PRETTY_FUNCTION__ << " with entry @ "
	    << std::hex
	    << getEntryAddr()
	    << std::dec
	    << "\n";
  std::cout << *this << "\n";
}

uint64_t regionCFG::countInsns() const {
  uint64_t cnt = 0;
  for(const basicBlock* bb : blocks) {
    cnt += bb->getNumIns();
  }
  return cnt;
}

uint64_t regionCFG::countBBs() const {
  return blocks.size();
}

uint64_t regionCFG::numBBInCommon(const regionCFG &other) const {
  bool other_larger = other.blocks.size() > blocks.size();
  uint64_t common = 0;
  const auto &large = other_larger ? other.blocks : blocks;
  const auto &small = other_larger ? blocks : other.blocks;

  for(const auto &bb : small) {
    auto it = large.find(bb);
    if(it != large.end()) {
      common++;
    }
  }
  
  return common;
}
 
void regionCFG::toposort(std::vector<cfgBasicBlock*> &topo) const {
  std::set<cfgBasicBlock*> visited;
  std::function<void(cfgBasicBlock*)> dfs = [&](cfgBasicBlock *bb) {
    assert(bb != nullptr);
    if(visited.find(bb) != visited.end())
      return;
    visited.insert(bb);
    for(auto nbb : bb->getSuccs()) {
      dfs(nbb);
    }
    topo.push_back(bb);
  };
  dfs(entryBlock);
  std::reverse(topo.begin(), topo.end());
}

void regionCFG::splitBBs() {
  bool needSplit = false;
  
  std::set<uint32_t> knownEntries;
  for(cfgBasicBlock *bb : cfgBlocks) {
    knownEntries.insert(bb->getEntryAddr());
  }
  
  do {
    needSplit = false;

    for(cfgBasicBlock *bb : cfgBlocks) {
      size_t ni = bb->rawInsns.size();
      if(ni < 2) {
	continue;
      }
      const basicBlock::insPair &termIns = bb->rawInsns.at(ni-2);
      uint32_t target = 0, fallthru = termIns.second + 8;
            
      if(isDirectBranchOrJump(termIns.first, termIns.second, target)) {
	bool knownTarget = (knownEntries.find(target) != knownEntries.end());
	bool knownFallthru = (knownEntries.find(fallthru) != knownEntries.end());

	if(knownTarget and knownFallthru) {
	  continue;
	}
	
	for(cfgBasicBlock *zbb : cfgBlocks) {
	  if(zbb->rawInsns.empty()) {
	    continue;
	  }
	  size_t n = zbb->rawInsns.size();
	  uint32_t fa = zbb->rawInsns.at(0).second;
	  uint32_t la = zbb->rawInsns.at(n-1).second;
	  uint32_t splitPoint = 0;

	  if(not(knownTarget) and (target > fa and target <= la)) {
	    splitPoint = target;
	    needSplit = true;
	  }
	  else if(not(knownFallthru) and (fallthru > fa and fallthru <= la)) {
	    splitPoint = fallthru;
	    needSplit = true;
	  }
	  
	  if(needSplit) {
	    
	    cfgBasicBlock *sbb = zbb->splitBB(splitPoint);
	    cfgBlocks.push_back(sbb);
	    /*
	    std::cerr << std::hex << "adding edge from "
		      << bb->getEntryAddr()
		      << " to "
		      << sbb->getEntryAddr()
		      << std::dec
		      << "\n";
	    */
	    bb->addSuccessor(sbb);
	    knownEntries.insert(sbb->getEntryAddr());
	    break;
	  }

	}
      }
      if(needSplit) {
	break;
      }
    }
  }
  while(needSplit);
}

void regionCFG::doLiveAnalysis(std::ostream &out) const {
  
  //for(const cfgBasicBlock *BB : cfgBlocks) {
  //    for(const Insn *I : BB->getInsns()) {
  //
  //}
  //}
    
  //
}
