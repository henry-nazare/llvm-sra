#include "SymbolicRangeAnalysis.h"

#define DEBUG_TYPE "sra-gen-test"

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

class SymbolicRangeAnalysisGenTest : public ModulePass {
public:
  static char ID;
  SymbolicRangeAnalysisGenTest() : ModulePass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnModule(Module&);
};

static RegisterPass<SymbolicRangeAnalysisGenTest>
  X("sra-gen-test", "Symbolic range analysis range generation test");
char SymbolicRangeAnalysisGenTest::ID = 0;

void SymbolicRangeAnalysisGenTest::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<SymbolicRangeAnalysis>();
  AU.setPreservesAll();
}

bool SymbolicRangeAnalysisGenTest::runOnModule(Module& M) {
  for (auto &F : M) {
    if (F.isIntrinsic() || F.isDeclaration())
      continue;

    auto &SRA = getAnalysis<SymbolicRangeAnalysis>(F);

    for (auto &BB : F) {
      IRBuilder<> IRB(BB.getTerminator());
      std::vector<Instruction*> Ins;
      for (auto &I : BB)
        if (I.getType()->isIntegerTy()) {
          Ins.push_back(&I);
        }

      for (auto I : Ins) {
        DEBUG(dbgs() << "Generating ranges: " << SRA.getStateOrInf(I)
                     << " for instruction " << *I << "\n");
        SRA.getRangeValuesFor(I, IRB);
      }
    }
  }

  return false;
}

