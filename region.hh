#ifndef __REGION_HH__
#define __REGION_HH__

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>

#include "basicBlock.hh"

class region {
 private:
  const static size_t nRegionHeads = 1024;
  /* constructor list initialized */
  size_t numEntries, hotThresh;
  /* other stuff in constructor */
  std::vector<basicBlock*> blockCache;
  std::array<basicBlock*, nRegionHeads> regionHeads;
  std::array<size_t, nRegionHeads> regionCounts;
  bool enHistCollection = false;
  size_t histBufPos = 0, tooLongAborts = 0;
  basicBlock *currRegionHead = nullptr;
  uint32_t computeIndex(uint32_t pc) const {
    return (pc >> 2) & (nRegionHeads-1);
  }
public:
  region(size_t lgNumEntries, size_t hotThresh) :
    numEntries(1UL<<lgNumEntries), hotThresh(hotThresh) {
    blockCache.resize(numEntries, nullptr);
    regionHeads.fill(nullptr);
    regionCounts.fill(0);
  }  
  bool collectionEnabled() const {
    return enHistCollection;
  }
  uint64_t getRegionLen() const {
    return histBufPos;
  }
  void clear() {
    std::fill(blockCache.begin(), blockCache.end(), nullptr);
    regionHeads.fill(nullptr);
    regionCounts.fill(0);
    currRegionHead = nullptr;
    histBufPos = 0;
    enHistCollection = false;
  }
  bool update(basicBlock *bb);
  void getRegion(std::vector<basicBlock*> &region);
  void disableRegionCollection();
  void updateRegionHeads(basicBlock *bb, basicBlock *lbb);

};


#endif
