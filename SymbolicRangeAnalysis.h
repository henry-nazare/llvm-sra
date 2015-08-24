#ifndef _SYMBOLICRANGEANALYSIS_H_
#define _SYMBOLICRANGEANALYSIS_H_

#include "SraGraph.h"
#include "Redefinition.h"

#include "llvm/Pass.h"

using namespace llvm;

class SymbolicRangeAnalysis : public FunctionPass {
public:
  static char ID;
  SymbolicRangeAnalysis() : FunctionPass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnFunction(Function&);
};

#endif

