#include "SymbolicRangeAnalysis.h"

#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

class SymbolicRangeAnalysisVerifier : public FunctionPass {
public:
  static char ID;
  SymbolicRangeAnalysisVerifier() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnFunction(Function&);
};

static RegisterPass<SymbolicRangeAnalysisVerifier>
  X("sra-verifier", "Symbolic range analysis metadata verifier");
char SymbolicRangeAnalysisVerifier::ID = 0;

void SymbolicRangeAnalysisVerifier::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<SymbolicRangeAnalysis>();
  AU.setPreservesAll();
}

bool SymbolicRangeAnalysisVerifier::runOnFunction(Function& F) {
  auto &SRA = getAnalysis<SymbolicRangeAnalysis>();
  bool HasError = false;
  std::string Range;
  raw_string_ostream Stream(Range);

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (MDNode *MD = I.getMetadata("sra")) {
        Stream << SRA.getStateOrInf(&I);
        StringRef Expected = cast<MDString>(*MD->getOperand(0)).getString();
        if (Stream.str() != Expected) {
          if (!HasError) {
            errs() << "FAILED: " << F.getName() << "\n";
            HasError = true;
          }

          errs() << "ERROR: Ranges differ on instruction " << I << "\n";
          errs() << "       Expected " << Expected << " got " << Stream.str()
              << "\n";
        }

        Stream.str().clear();
      }
    }
  }

  if (!HasError) {
    errs() << "PASSED: " << F.getName() << "\n";
  }

  return false;
}

