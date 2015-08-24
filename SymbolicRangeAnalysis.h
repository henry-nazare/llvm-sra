#ifndef _SYMBOLICRANGEANALYSIS_H_
#define _SYMBOLICRANGEANALYSIS_H_

#include "SraGraph.h"
#include "Redefinition.h"

#include "llvm/Pass.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include <map>

using namespace llvm;

class SymbolicRangeAnalysis : public FunctionPass {
public:
  static char ID;
  SymbolicRangeAnalysis() : FunctionPass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnFunction(Function&);

  std::string makeName(Function *F, Value *V);
  void        setName(Value *V, std::string Name);
  std::string getName(Value *V) const;

  void initialize(Function *F);

  void handleIntInst(Instruction *I);
  void handleBranch(BranchInst *BI, ICmpInst *ICI);

  void createNarrowingFn(Value *LHS, Value *RHS,
                         CmpInst::Predicate Pred, BasicBlock *BB);

private:
  Redefinition *RDF_;
  SraGraph *Graph_;
  std::map<Value*, std::string> Name_;
  std::map<Value*, CmpInst::Predicate> Sigma_;
  std::map<Value*, std::vector<Value*>> Incoming_;
};

#endif

