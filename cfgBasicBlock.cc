#include <ostream>
#include <fstream>

#include "regionCFG.hh"
#include "helper.hh"
#include "globals.hh"

cfgBasicBlock::~cfgBasicBlock() {
  for(size_t i = 0; i < insns.size(); i++) 
    delete insns[i];
  for(size_t i = 0; i < phiNodes.size(); i++) 
    delete phiNodes[i];
}

void cfgBasicBlock::addWithInCFGEdges(regionCFG *cfg) {
  /* if this block has a branch, search if successor
   * is in CFG region */
  iBranchTypeInsn *iBranch = nullptr;
  jTypeInsn *jJump = nullptr;
  rTypeInsn *jrInsn = nullptr;
  fpBranchInsn *fBranch = nullptr;
  std::map<uint32_t,cfgBasicBlock*>::iterator mit0,mit1;
  uint32_t tAddr=~0,ntAddr=~0;

  /* this is crappy code */
  for(ssize_t i = insns.size()-1; i >= 0; i--) {
    iBranch = dynamic_cast<iBranchTypeInsn*>(insns[i]);
    if(iBranch) break;
    jJump = dynamic_cast<jTypeInsn*>(insns[i]);
    if(jJump) break;
    jrInsn = dynamic_cast<rTypeJumpRegInsn*>(insns[i]);
    if(jrInsn) break;
    fBranch = dynamic_cast<fpBranchInsn*>(insns[i]);
    if(fBranch) break;
  }
  
#define PRINT_ADDED_EDGE(TGT) {\
    std::cerr << __PRETTY_FUNCTION__ << "@" << __LINE__ << " edge : " \
	      << std::hex << getEntryAddr() << " -> " << TGT->getEntryAddr() << std::dec << "\n"; \
  }
  
  if(iBranch) {
    tAddr = iBranch->getTakenAddr();
    ntAddr = iBranch->getNotTakenAddr();
    mit0 = cfg->cfgBlockMap.find(tAddr);
    mit1 = cfg->cfgBlockMap.find(ntAddr);
    if(mit0 != cfg->cfgBlockMap.end()) {
      //PRINT_ADDED_EDGE(mit0->second);
      addSuccessor(mit0->second); 
    }
    if(mit1 != cfg->cfgBlockMap.end()) {
      //PRINT_ADDED_EDGE(mit1->second);
      addSuccessor(mit1->second); 
    }
  }
  else if(fBranch) {
    tAddr = fBranch->getTakenAddr();
    ntAddr = fBranch->getNotTakenAddr();
    mit0 = cfg->cfgBlockMap.find(tAddr);
    mit1 = cfg->cfgBlockMap.find(ntAddr);

    if(mit0 != cfg->cfgBlockMap.end()) {
      //PRINT_ADDED_EDGE(mit0->second);
      addSuccessor(mit0->second); 
    }
    if(mit1 != cfg->cfgBlockMap.end()) {
      //PRINT_ADDED_EDGE(mit1->second);
      addSuccessor(mit1->second); 
    }
  }
  else if(jJump) {
    tAddr = jJump->getJumpAddr();
    mit0 = cfg->cfgBlockMap.find(tAddr);
    if(mit0 != cfg->cfgBlockMap.end()) {
      //PRINT_ADDED_EDGE(mit0->second);
      addSuccessor(mit0->second);
    }
  }
  /* end-of-block with no branch or jump */
  else if(!jrInsn) {
    tAddr = getExitAddr()+4;
    mit0 = cfg->cfgBlockMap.find(tAddr);
    if(mit0 != cfg->cfgBlockMap.end()) {
      //PRINT_ADDED_EDGE(mit0->second);
      addSuccessor(mit0->second);
    }
  }

}

bool cfgBasicBlock::checkIfPlausableSuccessors() {
  /* find terminating instruction */
  iBranchTypeInsn *iBranch = nullptr;
  for(size_t i = 0,n=insns.size(); i < n; i++) {
    iBranch = dynamic_cast<iBranchTypeInsn*>(insns[i]);
    if(iBranch) 
      break;
  }
  if(!iBranch)
    return true;

  bool impossible = false;


  for(auto cbb : succs) {
    if(cbb->insns.empty())
      continue;
    Insn *ins = cbb->insns[0];
    uint32_t eAddr = ins->getAddr();
    if(iBranch->getTakenAddr() == eAddr || iBranch->getNotTakenAddr() == eAddr)
      continue;
    impossible |= true;
  }
  
  return not(impossible);
}

void cfgBasicBlock::addSuccessor(cfgBasicBlock *s) {
  succs.insert(s);
  s->preds.insert(this);
}

void cfgBasicBlock::delSuccessor(cfgBasicBlock *s) {
  std::set<cfgBasicBlock*>::iterator sit = succs.find(s);
  succs.erase(sit);
  sit = s->preds.find(this);
  s->preds.erase(sit);
}

void cfgBasicBlock::addPhiNode(gprPhiNode *phi) {
  uint32_t r = phi->destRegister();
  if(gprPhis[r])
    delete phi;
  else {
    phiNodes.push_back(phi);
    gprPhis[r] = phi;
  }
}
void cfgBasicBlock::addPhiNode(hiloPhiNode *phi) {
  uint32_t r = phi->destRegister();
  if(hiLoPhis[r])
    delete phi;
  else {
    phiNodes.push_back(phi);
    hiLoPhis[r] = phi;
  }
}
void cfgBasicBlock::addPhiNode(fprPhiNode *phi) {
  uint32_t r = phi->destRegister();
  if(fprPhis[r])
    delete phi;
  else {
    phiNodes.push_back(phi);
    fprPhis[r] = phi;
  }
}
void cfgBasicBlock::addPhiNode(fcrPhiNode *phi) {
  uint32_t r = phi->destRegister();
  if(fcrPhis[r])
    delete phi;
  else {
    phiNodes.push_back(phi);
    fcrPhis[r] = phi;
  }
}
void cfgBasicBlock::addPhiNode(icntPhiNode *phi) {
  if(icntPhis[0])
    delete phi;
  else {
    phiNodes.push_back(phi);
    icntPhis[0] = phi;
  }
}

void cfgBasicBlock::computeConstGPRs() {
 
  for(size_t i = 0; i < insns.size(); i++) {
    Insn *ins = insns[i];
    ins->updateGPRConstants(gprConstState);
  }
  //for(size_t i = 0; i < 32; i++) {
  //if(gprConstState[i].e == constant) {
  //printf("reg %s is constant!\n", getGPRName(i,false).c_str());
  //}
  //}
}

bool cfgBasicBlock::has_jr_jalr() {
  for(size_t i = 0; i < insns.size(); i++) {
    Insn *ins = insns[i];
    if(dynamic_cast<insn_jr*>(ins))
      return true;
    if(dynamic_cast<insn_jalr*>(ins))
      return true;
  }
  return false;
}

bool cfgBasicBlock::haslikely() {
  for(size_t i = 0; i < insns.size(); i++) {
    Insn *ins = insns[i];
    if(ins->isLikelyBranch()) {
      if(!hasBranchLikely()) {
	printf("mismatch in block about likely!!\n");
	print();
	exit(-1);
      }
      return true;
    }
  }
  return false;
}

bool cfgBasicBlock::hasFloatingPoint(uint32_t *typeCnts) const {
  bool hasFP = false;
  for(const Insn* I : insns) {
    opPrecType oType = I->getPrecType();
    hasFP |= (oType==singleprec);
    hasFP |= (oType==doubleprec);
    typeCnts[oType]++;
  }
  return hasFP;
}

bool cfgBasicBlock::canCompile() const {
  for(size_t i = 0, len = insns.size(); i < len; i++) {
    if(not(insns[i]->canCompile())) {
      return false;
    }
  }
  return true;
}

uint32_t cfgBasicBlock::getExitAddr() const {
  return insns.empty() ? ~(0U) : insns.at(insns.size()-1)->getAddr();
}

std::string cfgBasicBlock::getName() const {
  return insns.empty() ? std::string("ENTRY") : toStringHex(insns.at(0)->getAddr());
}

std::ostream &operator<<(std::ostream &out, const cfgBasicBlock &bb) {
  if(bb.isLikelyPatch) 
    out << "isLikelyPatch...\n";
  out << "Successors of block: ";
  for(auto cBB : bb.succs) {
    if(!cBB->insns.empty()) {
      stream_hex(out, cBB->insns[0]->getAddr());
    }
    else
      out << "ENTRY ";
  }
  out << std::endl;
  out << "Predecessors of block: ";
  for(auto cBB : bb.preds) {
    if(!cBB->insns.empty()) {
      stream_hex(out,cBB->insns[0]->getAddr()); 
    }
    else
      out << "ENTRY ";
  }
  out << std::endl;
  for(auto ins : bb.insns) {
    out << *ins;
  }
  return out;
}


void cfgBasicBlock::print() {
  std::cerr << *this << std::endl;
}





bool cfgBasicBlock::patchLikely(regionCFG *cfg, cfgBasicBlock *pbb) {
  auto brInsn = dynamic_cast<abstractBranch*>(insns[insns.size()-1]);
  if(brInsn==nullptr) {
    printf("couldn't access branch instruction!\n");
    exit(-1);
  }
  uint32_t takenAddr = brInsn->getTakenAddr();
  bool patched = false;
  for(auto cbb : succs) {
    uint32_t nEntryAddr = cbb->getEntryAddr();
    if(nEntryAddr == takenAddr) {
      delSuccessor(cbb);
      addSuccessor(pbb);
      pbb->addSuccessor(cbb);
      patched = true;
      //printf("CREATED PATCH FOR %x\n", getEntryAddr());
      break;
    }
  }
  if(!patched) {
    /*predicting opposite of likely encoding */
    addSuccessor(pbb);
    patched = true;
  }

  return patched;
}

cfgBasicBlock* cfgBasicBlock::splitBB(uint32_t splitpc) {
  assert(not(isLikelyPatch));
  ssize_t offs = -1;
#if 0
  std::cout << "old:\n";
  for(size_t i = 0, n = rawInsns.size(); i < n; i++) {
    std::cout << std::hex << rawInsns.at(i).second << std::dec << " : ";
    disassemble(std::cout, rawInsns.at(i).first, rawInsns.at(i).second);
    std::cout << "\n";
  }
#endif
  
  cfgBasicBlock *sbb = new cfgBasicBlock(bb, false);
  sbb->rawInsns.clear();
  sbb->hasTermBranchOrJump = hasTermBranchOrJump;

  for(size_t i = 0, n = rawInsns.size(); i < n; i++) {
    if(rawInsns.at(i).second == splitpc) {
      offs = i;
      break;
    }
  }
  //std::cout << "offset @ " << offs << "\n";
  assert(offs != -1);
  
  for(size_t i = offs, n = rawInsns.size(); i < n; i++) {
    sbb->rawInsns.push_back(rawInsns.at(i));
  }
  rawInsns.erase(rawInsns.begin()+offs,rawInsns.end());
  
#if 0
  std::cout << "split at pc " << std::hex << splitpc << std::dec << "\n";
  std::cout << "split 0:\n";
  for(size_t i = 0, n = rawInsns.size(); i < n; i++) {
    std::cout << std::hex << rawInsns.at(i).second << std::dec << " : ";
    disassemble(std::cout, rawInsns.at(i).first, rawInsns.at(i).second);
    std::cout << "\n";
  }
  
  std::cout << "split 1:\n";
  for(size_t i = 0, n = sbb->rawInsns.size(); i < n; i++) {
    std::cout << std::hex << sbb->rawInsns.at(i).second << std::dec << " : ";
    disassemble(std::cout, sbb->rawInsns.at(i).first, sbb->rawInsns.at(i).second);
    std::cout << "\n";
  }
#endif
  
  /* attribute all successors to sbb */
  for(cfgBasicBlock *nbb : succs) {
    //std::cout << "succ @ " << std::hex <<  nbb->getEntryAddr() << std::dec << "\n";
    auto it = nbb->preds.find(this);
    assert(it != nbb->preds.end());
    nbb->preds.erase(it);
    sbb->addSuccessor(nbb);
  }
  succs.clear();
#if 0
  std::cout << std::hex
	    << getEntryAddr()
	    << " has a succ @ "
	    <<  sbb->getEntryAddr()
	    << std::dec
	    << "\n";
#endif
  addSuccessor(sbb);

  assert(succs.size() == 1);
#if 0
  for(cfgBasicBlock *nbb : succs) {
    std::cout << std::hex << getEntryAddr()
	      << " has a succ @ "  <<  nbb->getEntryAddr() << std::dec << "\n";
  }
#endif
  hasTermBranchOrJump = false;
  
  return sbb;
}

cfgBasicBlock::cfgBasicBlock(basicBlock *bb, bool isLikelyPatch) :
  bb(bb), isLikelyPatch(isLikelyPatch),
  hasTermBranchOrJump(false),
  lBB(nullptr),
  idombb(nullptr) {
  
  fprTouched.resize(32, fprUseEnum::unused);
  gprPhis.fill(nullptr);
  fprPhis.fill(nullptr);
  fcrPhis.fill(nullptr);
  hiLoPhis.fill(nullptr);
  icntPhis.fill(nullptr);
  
  for(size_t i = 0; i < 32; i++) {
    if(i==0) {
      regState rs = {constant,0};
      gprConstState.push_back(rs);
    }
    else {
      regState rs = {uninit,0xffffffff};
      gprConstState.push_back(rs);
    }
  }

  if(bb) {
    ssize_t numInsns = bb->getVecIns().size();
    if(isLikelyPatch) {
      rawInsns.push_back(bb->getVecIns()[numInsns-1]);
    }
    else {
      if(bb->hasBranchLikely()) {
	numInsns--;
      }
      for(ssize_t i = 0; i < numInsns; i++) {
	const basicBlock::insPair &p = bb->getVecIns()[i];
	rawInsns.push_back(p);
      }
    }
  }
}

void cfgBasicBlock::bindInsns(regionCFG *cfg) {
  if(not(insns.empty())) {
    for(size_t i = 0, n=insns.size(); i < n; i++) {
      delete insns[i];
    }
    insns.clear();
  }

  for(const auto & p : rawInsns) {
    Insn *ins = getInsn(p.first, p.second);
    ins->set(cfg,this);
    insns.push_back(ins);
  }
}


void cfgBasicBlock::updateFPRTouched(uint32_t reg, fprUseEnum useType) {
  const uint32_t mask = ~1U;

  //std::cerr << "reg = " << reg << " new use = " << useType 
  //<< " old type = " << fprTouched[reg] << std::endl;
  
  if(useType == fprUseEnum::doublePrec) {
    switch(fprTouched[reg])
      {
      case fprUseEnum::unused:
	fprTouched[(reg & mask)+0] = fprUseEnum::doublePrec;
	switch(fprTouched[(reg & mask)+1])
	  {
	  case fprUseEnum::unused:
	    fprTouched[(reg & mask)+1] = fprUseEnum::doublePrec;
	    break;
	  case fprUseEnum::singlePrec:
	    fprTouched[(reg & mask)+0] = fprUseEnum::both;
	    fprTouched[(reg & mask)+1] = fprUseEnum::both;
	    break;
	  default:
	    break;
	  }
	break;
      case fprUseEnum::singlePrec:
	fprTouched[(reg & mask)+0] = fprUseEnum::both;
	fprTouched[(reg & mask)+1] = fprUseEnum::both;
	break;
      case fprUseEnum::doublePrec:
      case fprUseEnum::both:
	break;
      default:
	die();
      }
  }
  else if(useType == fprUseEnum::singlePrec) {
    switch(fprTouched[reg])
      {
      case fprUseEnum::unused:
	fprTouched[reg] = fprUseEnum::singlePrec;
	break;
      case fprUseEnum::doublePrec:
	fprTouched[(reg & mask)+0] = fprUseEnum::both;
	fprTouched[(reg & mask)+1] = fprUseEnum::both;
	break;
      case fprUseEnum::singlePrec:
      case fprUseEnum::both:
	break;
      default:
	die();
      }
  }
  else {
    die();
  }
}

bool cfgBasicBlock::dominates(const cfgBasicBlock *B) const {
  if(this==B)
    return true;
  if(B->idombb == this)
    return true;
  if(idombb == B)
    return false;
  
  cfgBasicBlock *P = B->idombb;
  while(P) {
    if(P==this)
      return true;
    else if(P->preds.empty())
      break;
    else
      P = P->idombb;
  }
  return false;
}

llvm::BasicBlock *cfgBasicBlock::getSuccLLVMBasicBlock(uint32_t pc) {
  for(cfgBasicBlock *cbb : succs) {
    if(cbb->getEntryAddr() == pc) {
      return cbb->lBB;
    }
  }
  //die();
  return nullptr;
}

void cfgBasicBlock::traverseAndRename(regionCFG *cfg){
  cfg->myIRBuilder->SetInsertPoint(lBB);
  /* this only gets called for the entry block */
  llvmRegTables regTbl(cfg);
  for(size_t i = 0; i < 32; i++) {
    if(cfg->allGprRead[i] or not(cfg->gprDefinitionBlocks[i].empty())) {
      regTbl.loadGPR(i);
    }
  }
  if(cfg->allHiloRead[0] or not(cfg->hiloDefinitionBlocks.empty()) ) {
    regTbl.loadHiLo(0);
    regTbl.loadHiLo(1);
  }
  for(size_t i = 0; i < 32; i++) {
    if(cfg->allFprRead[i] or not(cfg->fprDefinitionBlocks[i].empty())) {
      regTbl.loadFPR(i);
    }
  }
  for(size_t i = 0; i < 5; i++) {
    if(cfg->allFcrRead[i] or not(cfg->fcrDefinitionBlocks[i].empty())) {
      regTbl.loadFCR(i);
    }
  }
  if(globals::countInsns) {
    regTbl.initIcnt();
  }

  //lBB->dump();

  termRegTbl.copy(regTbl);
  for(auto nBlock : dtree_succs) {
    /* iterate over instructions */
    cfg->myIRBuilder->CreateBr(nBlock->lBB);
    /* pre-order traversal */
    nBlock->traverseAndRename(cfg, regTbl);
    break;
  }
}

void cfgBasicBlock::patchUpPhiNodes(regionCFG *cfg) {
  for(phiNode* p : phiNodes) {
    for(cfgBasicBlock* bb :  preds) {
      p->addIncomingEdge(cfg, bb);
    }
  }
  
  for(cfgBasicBlock* bb : dtree_succs) {
    bb->patchUpPhiNodes(cfg);
  }
}

void cfgBasicBlock::traverseAndRename(regionCFG *cfg, llvmRegTables prevRegTbl) {
  llvmRegTables regTbl(prevRegTbl);

  /* this gets called for all other blocks block */
  cfg->myIRBuilder->SetInsertPoint(lBB);

  bool innerBlock = false;
  if(cfg->getInnerPerfectBlock()) {
    size_t nestingDepth  = cfg->loopNesting.size();
    innerBlock = cfg->loopNesting[nestingDepth-1][0].inSingleBlockLoop(this);
  }
  if(innerBlock) {
    cfg->getInnerPerfectBlock() = this;
  }


  for(auto p : phiNodes) {
    p->makeLLVMPhi(cfg, regTbl);
  }
  //bb->print();

  if(globals::countInsns) {
    regTbl.incrIcnt(insns.size());
  }

  if(globals::simPoints and insns.size()) {
    if(cfg->builtinFuncts.find("log_bb") != cfg->builtinFuncts.end()) {
      llvm::Value *vAddr = llvm::ConstantInt::get(cfg->type_int32, getEntryAddr());
      std::vector<llvm::Value*> argVector;
      argVector.push_back(vAddr);
      argVector.push_back(regTbl.iCnt);
      llvm::ArrayRef<llvm::Value*> cArgs(argVector);
      cfg->myIRBuilder->CreateCall(cfg->builtinFuncts["log_bb"],cArgs);
    }
  }
  
  /* generate code for each instruction */
  for(size_t i = 0, n=insns.size(); i < n; i++) {
    Insn *nInst = 0;
    if((i+1) < insns.size())
      nInst = insns[i+1];

    /* branch delay means we need to skip inst */
    if(insns[i]->codeGen(this, nInst, regTbl))
      i++;
  }

  termRegTbl.copy(regTbl);
  
  /* walk dominator tree */
  for(auto cbb : dtree_succs){
    /* pre-order traversal */
    cbb->traverseAndRename(cfg, regTbl);
  }
  
  if(not(hasTermBranchOrJump)) {
    uint32_t npc = getExitAddr() + 4;

    llvm::BasicBlock *nBB = 0;
    if(isLikelyPatch) {
      if(succs.empty()) {
	cfgBasicBlock *pBB = (*preds.begin());
	auto *lBranch = dynamic_cast<abstractBranch*>(pBB->insns[pBB->insns.size()-1]);
	assert(lBranch!=nullptr);
	nBB = cfg->generateAbortBasicBlock(lBranch->getTakenAddr(), regTbl, this, 0);
      }
      else {
	nBB = (*(succs.begin()))->lBB;
      }
    }
    else {
      nBB = getSuccLLVMBasicBlock(npc);
      nBB = cfg->generateAbortBasicBlock(npc, regTbl, this, nBB);
    }
    cfg->myIRBuilder->SetInsertPoint(lBB);
    //print();
    cfg->myIRBuilder->CreateBr(nBB);
    //lBB->dump();
  }

}

uint32_t cfgBasicBlock::getEntryAddr() const {
  if(not(rawInsns.empty())) {
    return rawInsns.at(0).second;
  }
  else if(bb != nullptr) {
    assert(false);
    return bb->getEntryAddr();
  }
  return ~(0U);
}
