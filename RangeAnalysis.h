#ifndef _SYMBOLICRANGEANALYSIS_H_
#define _SYMBOLICRANGEANALYSIS_H_

// Should always be the first include.
#include "SAGE/Python/PythonInterface.h"

#include "RAGraph.h"

#include "SAGE/SAGENameVault.h"
#include "SAGE/SAGERange.h"

#include "llvm/Pass.h"

using namespace llvm;

class RangeAnalysis : public FunctionPass {
public:
  static char ID;
  RangeAnalysis() : FunctionPass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnFunction(Function&);

  SAGERange getRange(Value *V);

  virtual void print(raw_ostream &OS, const Module*) const;

private:
  RAGraph *G_;
  SAGENameVault SNV_;
};

#endif

