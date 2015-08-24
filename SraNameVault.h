#ifndef _SRANAMEVAULT_H_
#define _SRANAMEVAULT_H_

#include "llvm/IR/Value.h"

#include <map>

using namespace llvm;

class SraNameVault {
public:
  std::string getName(Value *V);

private:
  std::string makeName(Value *V) const;

  std::map<Value*, std::string> Name_;
};

#endif

