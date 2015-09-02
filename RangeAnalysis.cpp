//===---------------------- RangeAnalysis.cpp ---------------------===//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ra"

#include "SAGE/Python/PythonInterface.h"

#include "RangeAnalysis.h"
#include "Redefinition.h"

#include "llvm/Support/Debug.h"

raw_ostream& operator<<(raw_ostream& OS, const RangeAnalysis& SRR) {
  SRR.print(OS, nullptr);
  return OS;
}

using namespace llvm;

static RegisterPass<RangeAnalysis>
  X("ra", "Numerical range analysis with SAGE");
char RangeAnalysis::ID = 0;

void RangeAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<PythonInterface>();
  AU.addRequired<Redefinition>();
  AU.setPreservesAll();
}

bool RangeAnalysis::runOnFunction(Function& F) {
  DEBUG(dbgs() << "SRA: runOnFunction: " << F.getName() << "\n");
  G_ = new RAGraph(&F, getAnalysis<Redefinition>(), SNV_);
  G_->initialize();
  G_->solve();
  return false;
}

SAGERange RangeAnalysis::getRange(Value *V) {
  return G_->getRange(V);
}

void RangeAnalysis::print(raw_ostream &OS, const Module*) const {
  OS << *G_ << "\n";
}
