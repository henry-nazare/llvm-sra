#ifndef _RAGRAPH_H_
#define _RAGRAPH_H_

#include "RAGraphBase.h"

using namespace llvm;

class RAGraph : public RAGraphBase {
public:
  RAGraph(Function *F, Redefinition &RDF, SAGENameVault &SNV);

private:
  virtual void addValue(Value *V);
};

#endif

