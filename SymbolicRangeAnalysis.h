#ifndef _SYMBOLICRANGEANALYSIS_H_
#define _SYMBOLICRANGEANALYSIS_H_

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

