#ifndef __ssainsn_hh__
#define __ssainsn_hh__

#include <array>
#include <set>
#include <vector>

template <typename T>
class MipsRegTable {
public:
  std::array<T*, 32> fprTbl;
  std::array<T*, 32> gprTbl;
  std::array<T*, 2> hiloTbl;
  std::array<T*, 5> fcrTbl;
public:
  MipsRegTable() {
    fprTbl.fill(nullptr);
    gprTbl.fill(nullptr);
    hiloTbl.fill(nullptr);
    fcrTbl.fill(nullptr);
  }
  MipsRegTable(const MipsRegTable &other) :
    fprTbl(other.fprTbl),
    gprTbl(other.gprTbl),
    hiloTbl(other.hiloTbl),
    fcrTbl(other.fcrTbl) {}
};

enum class insnDefType {no_dest,fpr,gpr,hilo,fcr,icnt,unknown};

class ssaInsn {
protected:
  insnDefType insnType;
  std::set<ssaInsn*> uses;
  std::vector<ssaInsn*> sources;
public:
  ssaInsn(insnDefType insnType = insnDefType::unknown) :
    insnType(insnType) {}
  virtual ~ssaInsn() {}
  void addUse(ssaInsn *u) {
    uses.insert(u);
  }
  const std::set<ssaInsn*>& getUses() const {
    return uses;
  }
  std::set<ssaInsn*>& getUses() {
    return uses;
  }
  const std::vector<ssaInsn*> &getSources() const {
    return sources;
  }
  std::vector<ssaInsn*> &getSources() {
    return sources;
  }
  insnDefType getInsnType() const {
    return insnType;
  }
  virtual uint32_t destRegister() const {
    return 0;
  }
  virtual bool isFloatingPoint() const {
    return false;
  }
};


#endif
