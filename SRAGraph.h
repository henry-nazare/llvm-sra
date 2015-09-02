#ifndef _SRAGRAPH_H_
#define _SRAGRAPH_H_

#include "RAGraphBase.h"

using namespace llvm;

class SRAGraph : public RAGraphBase {
public:
  SRAGraph(Function *F, Redefinition &RDF, SAGENameVault &SNV);

private:
  virtual void addValue(Value *V);
};

#endif

