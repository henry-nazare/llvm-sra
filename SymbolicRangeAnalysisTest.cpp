//===---------------------- SymbolicRangeAnalysis.cpp ---------------------===//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sra-test"

#include "SymbolicRangeAnalysis.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

struct CreateIfRet {
  BasicBlock *Then, *Else, *End;
};

class SymbolicRangeAnalysisTest : public ModulePass {
public:
  static char ID;
  SymbolicRangeAnalysisTest() : ModulePass(ID) { }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnModule(Module&);

  std::vector<Argument*> getArgs(Function *F);

  template <typename T>
  std::vector<SAGEExpr> getExprs(
      SymbolicRangeAnalysis *SRA, SAGEInterface *SI, std::vector<T> Values) {
    std::vector<SAGEExpr> Ret;
    for (auto V : Values) {
      Ret.push_back(SAGEExpr(*SI, SRA->getName(V)));
    }
    return Ret;
  }

  Function *createTestFunction(const char *Name, int NumArgs);
  IRBuilder<> createIRB(Function *F);
  BasicBlock *createBB(Function *F, const char *name);

  CreateIfRet createIfElse(IRBuilder<>& IRB, Value *Cond);
  CreateIfRet createIfElseWithUses(IRBuilder<>& IRB, ICmpInst *ICI);

  void createUse(IRBuilder<> IRB, Value *V, BasicBlock *BB);

  void assertRangeEq(SymbolicRangeAnalysis *SRA, Value *V, SAGERange Second);

  void testSimpleIf();


private:
  Module *Module_;
  LLVMContext *Context_;
};

static RegisterPass<SymbolicRangeAnalysisTest>
  X("sra-test", "IRBuilder tests for SRA");
char SymbolicRangeAnalysisTest::ID = 0;

void SymbolicRangeAnalysisTest::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Redefinition>();
  AU.addRequired<SymbolicRangeAnalysis>();
  AU.addRequired<SAGEInterface>();
}

bool SymbolicRangeAnalysisTest::runOnModule(Module& M) {
  Module_ = &M;
  Context_ = &M.getContext();

  testSimpleIf();

  return false;
}

std::vector<Argument*> SymbolicRangeAnalysisTest::getArgs(Function *F) {
  std::vector<Argument*> Ret;
  for (auto AI = F->arg_begin(), AE = F->arg_end(); AI != AE; ++AI)
    Ret.push_back(&(*AI));
  return Ret;
}

Function *SymbolicRangeAnalysisTest::createTestFunction(
    const char *Name, int NumArgs) {
  FunctionType *FunctionTy = FunctionType::get(
      Type::getVoidTy(*Context_),
      ArrayRef<Type*>(std::vector<Type*>(NumArgs, Type::getInt32Ty(*Context_))),
      false /* isVarArg */);

  Function *F = cast<Function>(Module_->getOrInsertFunction(Name, FunctionTy));

  // Create the entry block.
  createBB(F, "entry");

  return F;
}

IRBuilder<> SymbolicRangeAnalysisTest::createIRB(Function *F) {
  IRBuilder<> IRB(*Context_);
  IRB.SetInsertPoint(&F->getEntryBlock());
  return IRB;
}

BasicBlock *SymbolicRangeAnalysisTest::createBB(Function *F, const char *name) {
  return BasicBlock::Create(*Context_, name, F);
}

CreateIfRet SymbolicRangeAnalysisTest::createIfElse(
    IRBuilder<>& IRB, Value *Cond) {
  Function *F = IRB.GetInsertBlock()->getParent();

  CreateIfRet Ret;
  Ret.Then = createBB(F, "if.then");
  Ret.Else = createBB(F, "if.else");
  Ret.End = createBB(F, "if.end");
  IRB.CreateCondBr(Cond, Ret.Then, Ret.Else);
  IRB.SetInsertPoint(Ret.Then);
  IRB.CreateBr(Ret.End);
  IRB.SetInsertPoint(Ret.Else);
  IRB.CreateBr(Ret.End);

  return Ret;
}

CreateIfRet SymbolicRangeAnalysisTest::createIfElseWithUses(
    IRBuilder<>& IRB, ICmpInst *ICI) {
  auto If = createIfElse(IRB, ICI);
  createUse(IRB, ICI->getOperand(0), If.Then);
  createUse(IRB, ICI->getOperand(0), If.Else);
  createUse(IRB, ICI->getOperand(1), If.Then);
  createUse(IRB, ICI->getOperand(1), If.Else);
  return If;
}

void SymbolicRangeAnalysisTest::createUse(
    IRBuilder<> IRB, Value *V, BasicBlock *BB) {
  FunctionType *FunctionTy = FunctionType::get(
      Type::getVoidTy(*Context_),
      ArrayRef<Type*>(V->getType()),
      false /* isVarArg */);
  Constant *F = Module_->getOrInsertFunction("use", FunctionTy);
  IRB.SetInsertPoint(BB->getTerminator());
  IRB.CreateCall(F, ArrayRef<Value*>(V));
}

void SymbolicRangeAnalysisTest::assertRangeEq(
    SymbolicRangeAnalysis *SRA, Value *V, SAGERange Second) {
  SAGERange First = SRA->getState(V);
  if (!First.getLower().isEQ(Second.getLower())) {
    errs() << "ERROR: assertRangeEq: unmatched lower bound for value " << *V
           << ":\nExpected " << Second.getLower() << ", got "
           << First.getLower() << "\n";
  }
  if (!First.getUpper().isEQ(Second.getUpper())) {
    errs() << "ERROR: assertRangeEq: unmatched upper bound for value " << *V
           << ":\nExpected " << Second.getUpper() << ", got "
           << First.getUpper() << "\n";
  }
  DEBUG(dbgs() << "SRATest: range match: " << First << ", " << Second << "\n");
}

void SymbolicRangeAnalysisTest::testSimpleIf() {
  /* void test_simple_if(int a, int b) {
   *   if (a < b) {
   *     // a < b, b > a
   *     // Use "a".
   *     // Use "b".
   *   } else {
   *     // a >= b, b <= a
   *     // Use "a".
   *     // Use "b".
   *   }
   * }
   */
  Function *F = createTestFunction("test_simple_if", 2);
  IRBuilder<> IRB = createIRB(F);

  std::vector<Argument*> Args = getArgs(F);

  auto If =
      createIfElseWithUses(
          IRB, cast<ICmpInst>(IRB.CreateICmpSLT(Args[0], Args[1])));
  IRB.SetInsertPoint(If.End);
  IRB.CreateRetVoid();

  auto &SI = getAnalysis<SAGEInterface>();
  auto &RDF = getAnalysis<Redefinition>(*F);
  auto &SRA = getAnalysis<SymbolicRangeAnalysis>(*F);

  std::vector<SAGEExpr> Exprs = getExprs(&SRA, &SI, Args);

  assertRangeEq(
      &SRA, RDF.getRedef(Args[0], If.Then), SAGERange(Exprs[0], Exprs[1] - 1));
  assertRangeEq(
      &SRA, RDF.getRedef(Args[1], If.Then), SAGERange(Exprs[0] + 1, Exprs[1]));
  assertRangeEq(
      &SRA, RDF.getRedef(Args[0], If.Else), SAGERange(Exprs[1], Exprs[0]));
  assertRangeEq(
      &SRA, RDF.getRedef(Args[1], If.Else), SAGERange(Exprs[1], Exprs[0]));
}

