#include <cassert>
#include "region.hh"
#include "basicBlock.hh"
#include "helper.hh"
#define ELIDE_LLVM 1
#include "globals.hh"

void region::updateRegionHeads(basicBlock *bb, basicBlock *lbb) {
  assert(bb);
  assert(lbb);
  if(globals::profile)
    return;
  uint32_t bb_ea = bb->getEntryAddr();
  uint32_t lbb_ea = lbb->getEntryAddr();
  
  /* forward branch...*/
  if(bb_ea > lbb_ea /*|| bb->hasRegion*/ || enHistCollection)
    return;
  
  uint32_t idx = computeIndex(bb_ea);
  if(regionHeads[idx] == bb) {
    regionCounts[idx] += 1;
    if(regionCounts[idx] >= hotThresh) {
      enHistCollection = true;
      currRegionHead = bb;
      histBufPos = 0;
      regionHeads[idx] = nullptr;
    }
  }
  else {
    regionHeads[idx] = bb;
    regionCounts[idx] = 1;
  }
}

void region::disableRegionCollection() {
  if(enHistCollection){
    enHistCollection = false;
    if(currRegionHead) {
      uint32_t idx = computeIndex(currRegionHead->getEntryAddr());
      regionCounts[idx] = 0;
      regionHeads[idx] = nullptr;
    }
    currRegionHead = nullptr;
    histBufPos = 0;
  }
}

void region::getRegion(std::vector<basicBlock*> &region) {
  region.clear();
  uint32_t idx = computeIndex(currRegionHead->getEntryAddr());
  for(size_t i = 0; i < histBufPos; i++)
    region.push_back(blockCache.at(i));
  histBufPos = 0;
  currRegionHead = nullptr;
  regionCounts[idx] = 0;
  regionHeads[idx] = nullptr;
}

bool region::update(basicBlock *bb) {
  if(globals::profile)
    return false;
    
  if(enHistCollection) {
    if((bb == currRegionHead) && (histBufPos != 0)) {
      enHistCollection = false;
      return true;
    }
    else if(histBufPos == (numEntries-1)) {
      tooLongAborts++;
      enHistCollection = false;
      return false;
    }
    else {
      blockCache[histBufPos++] = bb;
      return false;
    }
  }
  return false;
}


