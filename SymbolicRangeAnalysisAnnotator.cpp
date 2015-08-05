#include "SymbolicRangeAnalysis.h"

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

class SymbolicRangeAnalysisAnnotator : public ModulePass {
public:
  static char ID;
  SymbolicRangeAnalysisAnnotator() : ModulePass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnModule(Module&);
};

static RegisterPass<SymbolicRangeAnalysisAnnotator>
  X("sra-annotator", "Symbolic range analysis annotator (metadata)");
char SymbolicRangeAnalysisAnnotator::ID = 0;

void SymbolicRangeAnalysisAnnotator::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<SymbolicRangeAnalysis>();
  AU.setPreservesAll();
}

bool SymbolicRangeAnalysisAnnotator::runOnModule(Module& M) {
  for (auto &F : M) {
    if (F.isIntrinsic() || F.isDeclaration())
      continue;

    auto &SRA = getAnalysis<SymbolicRangeAnalysis>(F);

    LLVMContext& C = M.getContext();
    std::string Range;
    raw_string_ostream Stream(Range);
    for (auto &BB : F)
      for (auto &I : BB)
        if (I.getType()->isIntegerTy()) {
          Stream << SRA.getStateOrInf(&I);
          I.setMetadata("sra", MDNode::get(C, MDString::get(C, Stream.str())));
          Stream.str().clear();
        }
  }

  return false;
}

