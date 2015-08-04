#ifndef _SYMBOLICRANGEANALYSIS_H_
#define _SYMBOLICRANGEANALYSIS_H_

#include "SAGE/SAGEInterface.h"

#include "Redefinition.h"

#include "SAGE/SAGEExpr.h"
#include "SAGE/SAGERange.h"

#include "llvm/Pass.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>

using namespace llvm;

class SymbolicRangeAnalysis : public FunctionPass {
public:
  static char ID;
  SymbolicRangeAnalysis() : FunctionPass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnFunction(Function&);
  virtual void print(raw_ostream &OS, const Module*) const;

  SAGEExpr  getBottomExpr() const;
  SAGERange getBottom() const;

  std::string makeName(Function *F, Value *V);
  void        setName(Value *V, std::string Name);
  std::string getName(Value *V) const;

  void      setState(Value *V, SAGERange Range);
  SAGERange getState(Value *V)      const;
  SAGERange getStateOrInf(Value *V) const;

  std::pair<Value*, Value*> getRangeValuesFor(Value *V, IRBuilder<> IRB) const;

  void initialize(Function *F);
  void reset(Function *F);
  void iterate(Function *F);
  void widen(Function *F);

  void handleIntInst(Instruction *I);
  void handleBranch(BranchInst *BI, ICmpInst *ICI);

  void createNarrowingFn(Value *LHS, Value *RHS,
                         CmpInst::Predicate Pred, BasicBlock *BB);

  void setChanged(Value *V, SAGERange &Prev, SAGERange &New);

  SAGEInterface &getSI() { return *SI_; }

private:
  Module *Module_;
  SAGEInterface *SI_;
  Redefinition  *RDF_;

  std::map<Value*, std::string> Name_;
  std::map<std::string, Value*> Value_;
  std::map<Value*, SAGERange>   State_;
  std::map<Value*, unsigned>    Changed_;

  std::map< Instruction*, std::function<SAGERange()> > Fn_;

  std::map<Instruction*, unsigned>             Mapping_;
  std::set<std::pair<unsigned, Instruction*> > Worklist_;
  std::set<Instruction*>                       Evaled_;
};

#endif

