//===-------------------------- SraNameVault.cpp --------------------------===//
//===----------------------------------------------------------------------===//

#include "SraNameVault.h"

#include "llvm/IR/Instructions.h"

std::string SraNameVault::getName(Value *V) {
  auto It = Name_.find(V);
  if (It != Name_.end()) {
    return It->second;
  }

  auto Name = makeName(V);
  Name_[V] = Name;
  return Name;
}

std::string SraNameVault::makeName(Value *V) const {
  static unsigned Temp = 1;

  std::string Prefix;
  if (Instruction *I = dyn_cast<Instruction>(V)) {
    Prefix = I->getParent()->getParent()->getName().str() + "_";
  } else if (Argument *A = dyn_cast<Argument>(V)) {
    Prefix = A->getParent()->getName().str() + "_";
  } else {
    Prefix = "GLOBAL_";
  }

  std::string Name;
  if (V->hasName()) {
    Name = V->getName().str();
  } else {
    Name = Twine(Temp).str();
    Temp = Temp + 1;
  }

  std::string Ret = Prefix + Name;
  std::replace(Ret.begin(), Ret.end(), '.', '_');
  return Ret;
}

