//===---------------------- SymbolicRangeAnalysis.cpp ---------------------===//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sra"

#include "SAGE/Python/PythonInterface.h"
#include "SymbolicRangeAnalysis.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/PostOrderIterator.h"

raw_ostream& operator<<(raw_ostream& OS, const SymbolicRangeAnalysis& SRR) {
  SRR.print(OS, nullptr);
  return OS;
}

using namespace llvm;

static RegisterPass<SymbolicRangeAnalysis>
  X("sra", "Symbolic range analysis with SAGE and QEPCAD");
char SymbolicRangeAnalysis::ID = 0;

void SymbolicRangeAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<PythonInterface>();
  AU.addRequired<Redefinition>();
  AU.setPreservesAll();
}

bool SymbolicRangeAnalysis::runOnFunction(Function& F) {
  RDF_ = &getAnalysis<Redefinition>();

  Sigma_.clear();
  Incoming_.clear();

  dbgs() << "SRA: runOnFunction: " << F.getName() << "\n";

  Graph_ = new SraGraph();
  initialize(&F);
  Graph_->solve();

  return false;
}

std::string SymbolicRangeAnalysis::makeName(Function *F, Value *V) {
  static unsigned Temp = 1;
  if (V->hasName()) {
    auto Name = (F->getName() + Twine("S") + V->getName()).str();
    std::replace(Name.begin(), Name.end(), '.', 'S');
    return Name;
  } else {
    auto Name = F->getName() + Twine("S") + Twine(Temp++);
    return Name.str();
  }
}

void SymbolicRangeAnalysis::setName(Value *V, std::string Name) {
  Name_[V] = Name;
}

std::string SymbolicRangeAnalysis::getName(Value *V) const {
  auto It = Name_.find(V);
  assert(It != Name_.end() && "Requested value is not in map");
  return It->second;
}

void SymbolicRangeAnalysis::createNarrowingFn(Value *LHS, Value *RHS,
                                      CmpInst::Predicate Pred, BasicBlock *BB) {
  if (PHINode *Redef = RDF_->getRedef(LHS, BB)) {
    Sigma_[Redef] = Pred;
    Incoming_[Redef] = std::vector<Value*>({Redef->getIncomingValue(0), RHS});
  }
}

void SymbolicRangeAnalysis::handleBranch(BranchInst *BI, ICmpInst *ICI) {
  Value *LHS = ICI->getOperand(0), *RHS = ICI->getOperand(1);
  BasicBlock *TB = BI->getSuccessor(0), *FB = BI->getSuccessor(1);
  CmpInst::Predicate Pred     = ICI->getPredicate(),
                     SwapPred = ICI->getSwappedPredicate(),
                     InvPred  = ICI->getInversePredicate(), EqPred;

  // For (i < j) branching to cond.true and cond.false, for example:
  // 1) i < j at cond.true;
  createNarrowingFn(LHS, RHS, Pred,     TB);
  // 2) j > i at cond.true;
  createNarrowingFn(RHS, LHS, SwapPred, TB);
  // 3) i >= j at cond.false;
  createNarrowingFn(LHS, RHS, InvPred,  FB);

  if (ICI->isEquality())
    EqPred = Pred;
  else if (ICI->isTrueWhenEqual())
    EqPred = (ICmpInst::Predicate)(Pred - 1);
  else {
    assert(ICI->isFalseWhenEqual());
    EqPred = (ICmpInst::Predicate)(Pred + 1);
  }
  // 4) j <= i at cond.false;
  createNarrowingFn(RHS, LHS, EqPred, FB);
}

void SymbolicRangeAnalysis::handleIntInst(Instruction *I) {
  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::SDiv:
    case Instruction::UDiv:
      Graph_->addBinOp(cast<BinaryOperator>(I), getName(I));
      Incoming_[I] = std::vector<Value*>({I->getOperand(0), I->getOperand(1)});
      break;
    case Instruction::PHI: {
      PHINode *Phi = cast<PHINode>(I);
      if (Sigma_.count(Phi)) {
        Graph_->addSigmaNode(Phi, Sigma_[Phi], getName(Phi));
        break;
      }
      Graph_->addPhiNode(Phi, getName(I));
      auto Incoming = Phi->incoming_values();
      Incoming_[I] = std::vector<Value*>(Incoming.begin(), Incoming.end());
      break;
    }
    // TODO: handle trunc, zext & sext.
    default:
      Graph_->addNamedConstant(I, getName(I));
  }
}

void SymbolicRangeAnalysis::initialize(Function *F) {
  // Create symbols for the function's integer arguments.
  for (auto AI = F->arg_begin(), AE = F->arg_end(); AI != AE; ++AI) {
    if (AI->getType()->isIntegerTy()) {
      std::string Name = makeName(F, &(*AI));
      setName(&(*AI), Name);
      Graph_->addNamedConstant(&(*AI), Name);
    }
  }

  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (I.getType()->isIntegerTy()) {
        setName(&I, makeName(F, &I));
      }
    }
  }

  ReversePostOrderTraversal<Function*> RPOT(F);

  // Create a closure for each instruction.
  for (auto RI = RPOT.begin(), RE = RPOT.end(); RI != RE; ++RI) {
    // Handle sigma nodes.
    TerminatorInst *TI = (*RI)->getTerminator();
    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      if (BI->isConditional()) {
        if (ICmpInst *ICI = dyn_cast<ICmpInst>(BI->getCondition())) {
          handleBranch(BI, ICI);
        }
      }
    }

    // Handle everything that's not a sigma node.
    for (auto &I : **RI) {
      if (I.getType()->isIntegerTy()) {
        handleIntInst(&I);
      }
    }
  }

  for (auto &P : Incoming_) {
    for (auto &Incoming : P.second) {
      Graph_->addIncoming(Incoming, P.first);
    }
  }
}

