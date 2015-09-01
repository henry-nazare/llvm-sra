#ifndef _SYMBOLICRANGEANALYSIS_H_
#define _SYMBOLICRANGEANALYSIS_H_

// Should always be the first include.
#include "SAGE/Python/PythonInterface.h"

#include "SRAGraph.h"

#include "SAGE/SAGENameVault.h"
#include "SAGE/SAGERange.h"

#include "llvm/Pass.h"

using namespace llvm;

class SymbolicRangeAnalysis : public FunctionPass {
public:
  static char ID;
  SymbolicRangeAnalysis() : FunctionPass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnFunction(Function&);

  SAGERange getRange(Value *V);

private:
  SRAGraph *G_;
  SAGENameVault SNV_;
};

#endif

