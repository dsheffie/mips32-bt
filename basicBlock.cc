#include "basicBlock.hh"
#include "disassemble.hh"
#include "helper.hh"
#include "compile.hh"
#include "interpret.hh"
#include "region.hh"
#include "regionCFG.hh"

#include <string>
#include <cstdlib>
#include <cstdio>
#include <functional>

#include "globals.hh"
#include "simPoints.hh"

uint64_t basicBlock::cfgCnt = 0;

bool basicBlock::canCompileRegion(std::vector<basicBlock*> &region ) {
  for(size_t bIdx = 0, tLen=region.size(); bIdx < tLen; bIdx++) {
    bool gotBrOrJmp = false;
    basicBlock *b = region.at(bIdx);
    basicBlock *nB = (bIdx == (region.size()-1)) ? region.at(0) : region.at(bIdx+1);
    for(size_t i = 0, vLen = b->vecIns.size(); i < vLen; i++) {
      uint32_t insn = b->vecIns[i].first;
      gotBrOrJmp |= isBranchOrJump(insn);
      if(!compile::canCompileInstr(insn)) {
	if(globals::verbose) {
	  uint32_t addr = b->vecIns[i].second;
	  std::cout << std::hex << addr << std::dec << ":"
		    << getAsmString(insn, addr)
		    << " : can't compile\n";
	}
	return false;
      }
    }
    if(not(gotBrOrJmp) and ((b->termAddr+4) != nB->entryAddr)) {
      return false;
    }
  }
  return true;
}


void basicBlock::repairBrokenEdges() {
  const ssize_t n_inst = vecIns.size();
  bool found_cflow_insn = false;
  assert(n_inst != 0);
  for(ssize_t i = n_inst - 1; (i >= 0) and not(found_cflow_insn); i--) {
    uint32_t inst = vecIns.at(i).first;
    if(is_jr(inst)) {
      found_cflow_insn = true;
    }
    else if(is_jal(inst)) {
      found_cflow_insn = true;
    }
    else if(is_j(inst)) {
      found_cflow_insn = true;
    }
    else if(is_branch(inst)) {
      found_cflow_insn = true;
    }
  }
  /* if a basicblock isn't terminated with a branch or jump
   * ensure that it only has one successor */
  if(not(found_cflow_insn) and (succs.size() != 1)) {
    const uint32_t nextpc = vecIns.at(n_inst-1).second + 4;
    while(succs.size() != 1) {
      for(basicBlock *nbb : succs) {
	if(nbb->getEntryAddr() != nextpc) {
	  auto it0 = succs.find(nbb);
	  succs.erase(it0);
	  auto it1 = nbb->preds.find(this);
	  if(it1 != nbb->preds.end()) {
	    nbb->preds.erase(it1);
	  }
	  break;
	}
      }
    }
  }

  
}

void basicBlock::dropAllBBs() {
  cfgCnt = 0;
  for(auto &p : bbMap) {
    delete p.second;
  }
  bbMap.clear();
  insMap.clear();
  insInBBCnt.clear();
  globals::cBB = nullptr;
  globals::regionFinder->disableRegionCollection();
}

void basicBlock::setReadOnly() {
  if(not(readOnly)) {
    readOnly = true;
    for(size_t i = 0, len = vecIns.size(); i < len; i++) {
      uint32_t insn = vecIns[i].first;
      hasjr |= is_jr(insn);
      hasjalr |= is_jalr(insn);
      hasjal |= is_jal(insn);
      if(hasjal) {
	uint32_t pc = entryAddr + i*4;
	uint32_t jaddr = insn & ((1<<26)-1);
	jaddr <<= 2;
	jaddr |= ((pc+4) & (~((1<<28)-1)));
      }
      hasmonitor |= is_monitor(insn);
    }
    if(cfgCplr) {
      cfgCplr=nullptr;
      delete cfgCplr;
    }
    hasRegion = isCompiled = false;
    bbRegions.clear();
    bbRegionCounts.clear();
  }
 }



void basicBlock::addSuccessor(basicBlock *bb) {
  succs.insert(bb);
  succsMap[bb->entryAddr] = bb;
  bb->preds.insert(this);

  
  if( globals::enableCFG and (succs.size() > 2) and not(hasjr or hasjalr or hasmonitor) ) {
    std::cerr << KRED << "ERROR: 0x" 
	      << std::hex << getEntryAddr() << std::dec
	      << " has " << succs.size() <<  " successors!"
	      << KNRM << std::endl;
    std::cerr << *this;
    for(auto succ : succs) {
      std::cerr << "\t0x" << std::hex << succ->getEntryAddr() << std::dec << "\n";
    }
    die();
  }
}

basicBlock::basicBlock(uint32_t entryAddr) : execUnit(), entryAddr(entryAddr) {
  bbMap[entryAddr] = this;  
}

basicBlock::basicBlock(uint32_t entryAddr, basicBlock *prev) : basicBlock(entryAddr) {
  prev->addSuccessor(this);
}

void basicBlock::addIns(uint32_t inst, uint32_t addr) {
  if(not(readOnly)) {
    vecIns.emplace_back(inst,addr);
#if 0
    if(vecIns.size() > 1024) {
      std::cerr << *this;
      std::cerr << "basicblock @ " << std::hex << entryAddr << std::dec << " is very large\n";
      die();
    }
#endif
    insMap[addr] = this;
    insInBBCnt[addr]++;
    
    //if(insInBBCnt[addr] > 1) {
    //std::cerr << *this;
    // exit(-1);
    //}
  }
}

void basicBlock::dropCompiledCode() {
  bbRegions.clear();
  bbRegionCounts.clear();
  for(basicBlock *nukeBB : cfgInRegions){
    if(nukeBB->cfgCplr) {
      delete nukeBB->cfgCplr;
      nukeBB->cfgCplr = nullptr;
      nukeBB->hasRegion = false;
    }
    nukeBB->bbRegions.clear();
    nukeBB->bbRegionCounts.clear();
  }  
}

basicBlock *basicBlock::split(uint32_t nEntryAddr) {
#if 0
  std::cerr << "split @ 0x" << std::hex << entryAddr << std::dec 
	    << " cfgInRegions.size() = " << cfgInRegions.size() 
	    << std::endl;
#endif
  size_t offs = (nEntryAddr-entryAddr) >> 2;
  bool splitAtLastInst = (offs == (vecIns.size()-1)) and isBranchOrJump(vecIns.at(offs-1).first);
  
  globals::regionFinder->disableRegionCollection();
  dropCompiledCode();

  basicBlock *nBB = new basicBlock(nEntryAddr);
  if(splitAtLastInst) {
    nBB->addIns(vecIns.at(offs).first, vecIns.at(offs).second);
    nBB->setReadOnly();
    nBB->termAddr = nEntryAddr;
    return nBB;
  }
  

  //unlink old successors and update with new block
  for(basicBlock *b : succs) {
    auto ssit = b->preds.find(this);
    b->preds.erase(ssit);
    nBB->succsMap[b->entryAddr] = b;
    nBB->succs.insert(b);
    b->preds.insert(nBB);
  }
  //add new successor
  succs.clear();
  succsMap.clear();

  succsMap[nBB->entryAddr] = nBB;
  succs.insert(nBB);
  nBB->preds.insert(this);
  
  for(size_t i = offs, len = vecIns.size(); i < len; i++) {
    uint32_t addr = i*4 + entryAddr;
    insMap[addr] = nBB;
    insInBBCnt[addr]++;
    nBB->vecIns.push_back(vecIns[i]);
  }
  
  vecIns.erase(vecIns.begin() + offs, vecIns.end());

  nBB->termAddr = termAddr;
  nBB->edgeCnts = edgeCnts;
  nBB->totalEdges = totalEdges;
  edgeCnts.clear();
  edgeCnts[nBB->entryAddr] = totalEdges;

  
  termAddr = nEntryAddr-4;
  readOnly = false;
  hasjr = hasjal = hasjalr = hasmonitor = false;
  setReadOnly();

  hasTermBranchOrJump = false;
  if( branchLikely ) {
    nBB->branchLikely = true;
    branchLikely = false;
  }
  nBB->inscnt = inscnt;
  nBB->setReadOnly(); 
  
  return nBB;
}

basicBlock *basicBlock::globalFindBlock(uint32_t entryAddr) {
  auto it = bbMap.find(entryAddr);
  if(it == bbMap.end())
    return nullptr;
  else
    return it->second;
}

basicBlock *basicBlock::localFindBlock(uint32_t entryAddr) {
  if(entryAddr == this->entryAddr)
    return this;

  const auto it = succsMap.find(entryAddr);
  if(it == succsMap.end())
    return nullptr;
  else
    return it->second;
}

basicBlock *basicBlock::findBlock(uint32_t entryAddr) {
  basicBlock *fBlock = nullptr;
  auto sIt = succsMap.find(entryAddr);
  
  if(sIt != succsMap.end()) {
    fBlock = sIt->second;
  }
  else {
    auto bIt =  bbMap.find(entryAddr);
    if(bIt != bbMap.end()) {
      fBlock = bIt->second;
      addSuccessor(fBlock);
    }
    else {
      auto iIt = insMap.find(entryAddr);
      if(iIt != insMap.end()) {
	basicBlock *sBB = iIt->second;
	basicBlock *nBB = sBB->split(entryAddr);
	fBlock = nBB;
	addSuccessor(fBlock);
      }
    }
  }
  return fBlock;
}

ssize_t basicBlock::sizeInBytes() const {
  if(not(readOnly))
    return -1;
  return 4*vecIns.size();
}

void basicBlock::print() const {
  std::cerr << *this;
}

std::ostream &operator<<(std::ostream &out, const basicBlock &bb) {
  using namespace std;
  out << "block  @" << hex << bb.entryAddr << dec 
      << "(cnt = " << bb.inscnt << "),"
      << "readOnly = " << bb.readOnly << "," 
      << "isCompiled = " << bb.isCompiled << "," 
      << "succs = " << bb.succs.size() << ","
      << "preds = " << bb.preds.size()
      << endl;
    
  for(size_t i = 0; i < bb.vecIns.size(); i++){
    uint32_t inst = bb.vecIns[i].first;
    uint32_t addr = bb.entryAddr + i*4;
    string asmString = getAsmString(inst, addr);
    out << hex << addr << dec << " : " << asmString << endl;
    if(addr == 0) {
      cerr << "instruction has address 0, how?\n";
      exit(-1);
    }
  }
  out << "successors:\n";
  for(auto sbb : bb.succs) {
    out << hex << sbb->getEntryAddr() << dec << "\n";
  }
  out << "predecessors:\n";
  for(auto pbb : bb.preds) {
    out << hex << pbb->getEntryAddr() << dec << "\n";
  }
  
  out << "terminal addr = " << hex 
      << bb.termAddr << dec << endl;

  return out;
}


basicBlock* basicBlock::run(state_t *s) {
  inscnt += getNumIns();
  globals::currUnit = this;
  ssize_t length = (termAddr-entryAddr)/4;
  if(globals::simPoints) {
    log_bb(s->pc, s->icnt);
  }
  if(globals::isMipsEL) {
    for(ssize_t i = 0; (i <= length) && (s->brk == 0); i++) { 
      interpretEL(s);
    }
  }
  else {
    for(ssize_t i = 0; (i <= length) && (s->brk == 0); i++) { 
      interpret(s);
    }
  }
  edgeCnts[s->pc]++;
  totalEdges++;
  return findBlock(s->pc);
}

bool basicBlock::execute(state_t *s) {
  basicBlock *nBB = nullptr;
  if(!readOnly || s->pc != entryAddr) {
    s->oldpc = ~0;
    return false;
  }
  nBB = this->run(s);
  if(nBB == nullptr) {
    nBB = new basicBlock(s->pc, globals::cBB);
  }
  globals::cBB = nBB;
  return true;
}

bool basicBlock::enoughRegions() const {
  for(auto tc : bbRegionCounts) {
    if(tc.second >= globals::enoughRegions)
      return true;
  }
  return false;

  //for(std::map<uint32_t, uint32_t>::iterator it = bbRegionCounts.begin();
  // it != bbRegionCounts.end(); ++it) {
  //if(it->second >= 5)
  //return true;
  //}
  //return false;
}

void basicBlock::addRegion(const std::vector<basicBlock*> &region) {
  uint32_t crc = ~0U;
  for(size_t i = 0, l = region.size(); i < l; i++) {
    crc = update_crc(crc, reinterpret_cast<uint8_t*>(&(region[i]->entryAddr)), 4);
  }
  crc ^= (~0U);
  if(bbRegionCounts.find(crc)==bbRegionCounts.end()) {
    bbRegions.push_back(region);
    bbRegionCounts[crc] = 1;
  }
  else {
    bbRegionCounts[crc] += 1;
  }
}

bool basicBlock::executeJIT(state_t *s) {
  std::vector<basicBlock*> bbRegion;
  bool gotRegion = false;
  basicBlock *nBB = nullptr;
  if(!readOnly || s->pc != entryAddr) {
    globals::regionFinder->disableRegionCollection();
    s->oldpc = ~0;
    return false;
  }
  
  if(/*not(globals::replay) and*/ globals::regionFinder->update(this)) {
    globals::regionFinder->getRegion(bbRegion);
    gotRegion = true;    
    for(size_t i = 0; i < bbRegion.size(); i++) {
#if 0
      std::cerr << "0x" << std::hex << bbRegion[i]->getEntryAddr() << std::dec 
		<< " added to region with head of 0x" 
		<< std::hex << getEntryAddr() << std::dec 
		<< std::endl;
#endif  
      bbRegion[i]->cfgInRegions.insert(this);
    }
  }
  
  if(gotRegion) {
    bool canCompile = canCompileRegion(bbRegion);
    addRegion(bbRegion);
    if(enoughRegions() and canCompile) {
      if(globals::enableCFG) {
	cfgCplr = new regionCFG();
	double now = timestamp();
	if(cfgCplr->buildCFG(bbRegions)) {
	  now = timestamp() - now;
#if 0
	  std::cout << now << " seconds to compile with "
		    << cfgCplr->countInsns() << " insns and "
		    << cfgCplr->countBBs() << " basic blocks\n";

	  std::set<basicBlock*> ss;
	  for(const auto &bbt : bbRegions) {
	    for(const auto &bb : bbt){
	      ss.insert(bb);
	    }
	  }
	  for(auto bb : ss) {
	    std::cout << std::hex
		      << bb->getEntryAddr()
		      << std::dec
		      << "\n";
	  }

	  fflush(nullptr);
#endif
	  bbRegions.clear();
	  bbRegionCounts.clear();
	  hasRegion = true;
	}
	else {
	  delete cfgCplr;
	  cfgCplr = nullptr;
	}
      }
    }
    else {
      hasRegion = false;
      if(not(canCompile)) {
	bbRegions.clear();
	bbRegionCounts.clear();
      }
    }
  }
  
  s->oldpc = s->pc;
  
  if(hasRegion and not(globals::regionFinder->collectionEnabled())) {
    if(cfgCplr)  {
      nBB = cfgCplr->run(s);
    }
  }
  else {
    nBB = this->run(s);
  }
  
  if(nBB == nullptr) {
    nBB = new basicBlock(s->pc, globals::cBB);
  }
  
  if(not(nBB->hasRegion) and globals::cBB) {
    globals::regionFinder->updateRegionHeads(nBB, globals::cBB);
  }
  globals::cBB = nBB;
  return true;
}


basicBlock::~basicBlock() {
  if(cfgCplr)
    delete cfgCplr;
}

void basicBlock::info() {
  std::cout << __PRETTY_FUNCTION__ << " with entry @ "
     	    << std::hex
     	    << getEntryAddr()
     	    << std::dec
	    << "\n";
}

void basicBlock::report(std::string &s, uint64_t icnt) {
  char buf[256];
  double frac = ((double)inscnt / (double)icnt)*100.0;
  bool hasFP = false, hasMonitor = false;
  for(size_t i = 0; i < vecIns.size(); i++) {
    hasFP |= isFloatingPoint(vecIns[i].first);
    hasMonitor |= is_monitor(vecIns[i].first);
  }
  sprintf(buf,"uncompiled basicblock(monitor=%d,fp=%d) @ 0x%x (inscnt=%zu, %g%%)\n", 
	  (int)hasMonitor, (int)hasFP, getEntryAddr(), (size_t)inscnt, frac);
  s += std::string(buf);
  debugSymDB::lookup(getEntryAddr(),s);
  sprintf(buf,"\n");
  std::stringstream ss;
  ss << *this;
  s += ss.str();
  s += "\n\n";
  s += std::string(buf);
}


bool basicBlock::validPath(std::vector<basicBlock*> &rpath) {
  for(ssize_t i = 0; i < (ssize_t)(rpath.size()-1); i++) {
    basicBlock *bb = rpath[i];
    if(bb->succs.find(rpath[i+1]) == bb->succs.end()) {
      printf("this block:\n"); 
      bb->print();
      printf("next block:\n"); 
      rpath[i+1]->print();
      return false;
    }
  }
  return true;
}


bool basicBlock::dfs(basicBlock* oldhead, std::set<basicBlock*> &visited, 
		     std::vector<basicBlock*> &path) {
  if(visited.find(this)!=visited.end())
    return false;

  visited.insert(this);
  for(basicBlock *nbb : succs) {
    if(nbb==oldhead) {
      path.push_back(this);
      return true;
    }
    if(nbb->dfs(oldhead, visited, path)) {
      path.push_back(this);
      return true;
    }
  }
  return false;
}

void basicBlock::setCFG(regionCFG *cfg) {
  assert(cfgCplr==nullptr);
  cfgCplr = cfg;
  hasRegion = true;
}


funcComplStatus findLeafNodeFunc(basicBlock* entryBB,
				 const std::map<uint32_t,std::pair<std::string, uint32_t>> &syms,
				 std::vector<basicBlock*> &func,
				 int &numErrors) {
  funcComplStatus rc = funcComplStatus::success;
  int numRet = 0;
  std::set<basicBlock*> seen;
  
  std::function<void(basicBlock*)> searcher = [&](basicBlock *bb) {
    bool hasRetInBlock = false;
    if(seen.find(bb)!=seen.end())
      return;

    seen.insert(bb);

    if(bb->hasJALR()) {
      rc = funcComplStatus::indirect_call;
      numErrors++;
      return;
    }
    else if(bb->hasJAL()) {
      auto s = *(bb->getSuccs().begin());
      uint32_t spc = s->getEntryAddr();
      basicBlock *inlineBB = basicBlock::globalFindBlock(spc);
      if(inlineBB == entryBB) {
	rc = funcComplStatus::recursive_call;
      }
      else {
	rc = funcComplStatus::direct_call;
      }
      numErrors++;
    }
    else if(bb->hasMONITOR()) {
      rc = funcComplStatus::monitor;
      numErrors++;
    }

    /* check if there's a jr */
    if(bb->hasJR(true)) {
      numRet++;
      hasRetInBlock = true;
    }
    else if(bb->hasJR(false)) {
      rc = funcComplStatus::arbitrary_jr;
      numErrors++;
    }
    
    /* if jal or jalr, no good */
    if(!hasRetInBlock) {
      for(auto nbb : bb->getSuccs()) {
	searcher(nbb);
      }
    }
    func.push_back(bb);
  };
  searcher(entryBB);
  std::reverse(func.begin(), func.end());
  if(numRet==0) {
    rc = (numRet==0) ? funcComplStatus::no_return :
      funcComplStatus::too_many_returns;
    numErrors++;
  }
  return rc;
}


funcComplStatus findFuncWithInline(basicBlock* entryBB,
				   const std::map<uint32_t, std::pair<std::string, uint32_t>> &syms,
				   const std::set<uint32_t> & leaf_funcs,
				   std::vector<basicBlock*> &func) {
  funcComplStatus rc = funcComplStatus::success;
  std::set<basicBlock*> seen;
  int numErrors = 0;
  
  std::function<void(basicBlock*)> searcher = [&](basicBlock *bb) {
    bool gotJal = false;
    if(seen.find(bb)!=seen.end())
      return;

    seen.insert(bb);

    if(bb->hasJALR()) {
      rc = funcComplStatus::indirect_call;
      numErrors++;
      return;
    }
    else if(bb->hasJAL()) {
      auto s = *(bb->getSuccs().begin());
      uint32_t spc = s->getEntryAddr();
      basicBlock *inlineBB = basicBlock::globalFindBlock(spc);
      if(inlineBB && (leaf_funcs.find(spc) != leaf_funcs.end())) {
	auto s = findLeafNodeFunc(inlineBB,syms,func,numErrors);
	assert(s==funcComplStatus::success);
	gotJal = true;
      }
      else {
	rc = funcComplStatus::direct_call;
	numErrors++;
      }
    }
    else if(bb->hasMONITOR()) {
      rc = funcComplStatus::monitor;
      numErrors++;
      return;
    }
    
    /* check if there's a jr to ra */
    bool retInBB = false;
    if(bb->hasJR(true)) {
      retInBB = true;
    }
    else if(bb->hasJR(false)) {
      rc = funcComplStatus::arbitrary_jr;
      numErrors++;
    }
    if(!(gotJal || retInBB)) {
      for(auto nbb : bb->getSuccs()) {
	searcher(nbb);
      }
    }
    else {
      uint32_t jaddr = ~0U;
      for(auto &p : bb->getVecIns())  {
	if(is_jal(p.first)) {
	  jaddr = p.second;
	  break;
	}
      }
      basicBlock *nbb = basicBlock::globalFindBlock(jaddr+8);
      if(nbb == nullptr) {
	std::cout << "CANT FIND LANDING PAD\n";
	rc = funcComplStatus::arbitrary_jr;
	return;
      }
	
      searcher(nbb);
    }
    func.push_back(bb);
  };
  searcher(entryBB);
  std::reverse(func.begin(), func.end());
  
  return rc;
}

void basicBlock::toposort(const std::set<basicBlock*> &valid, std::list<basicBlock*> &ordered, std::set<basicBlock*> &visited) {
  auto it = visited.find(this);
  if(it != visited.end())
    return;
  visited.insert(this);

  for(basicBlock *s : getSuccs()) {
    auto v = valid.find(s);
    if(v == valid.end())
      continue;
    s->toposort(valid, ordered, visited);
  }
  ordered.push_front(this);
}

void basicBlock::toposort(basicBlock *src, const std::set<basicBlock*> &valid, std::list<basicBlock*> &ordered) {
  std::set<basicBlock*> visited;
  src->toposort(valid, ordered, visited);
}

bool basicBlock::hasTermDirectBranchOrJump(uint32_t &target, uint32_t &fallthru) const {
  size_t ni = vecIns.size();
  assert(ni != 0);
  fallthru = vecIns.at(ni-1).second + 4;
  if(ni < 2) {
    return false;
  }
  return isDirectBranchOrJump(vecIns.at(ni-2).first, vecIns.at(ni-2).second, target);
  
}

