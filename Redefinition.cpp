//===------------------------- Redefinition.cpp ---------------------------===//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "redef"

// Python.h should always be the first included file.
#include "SAGE/Python/PythonInterface.h"

#include "Redefinition.h"

#include "SAGE/SAGEInterface.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <list>
#include <set>

#if LLVM_VERSION_MINOR >= 7
typedef LoopInfoWrapperPass LoopInfoPass;
#else
typedef LoopInfo LoopInfoPass;
#endif

STATISTIC(NumCreatedSigmas, "Number of sigma-phis created");
STATISTIC(NumCreatedFrontierPhis, "Number of non-sigma-phis created");

using namespace llvm;

static RegisterPass<Redefinition> X("redef", "Integer live-range splitting");
char Redefinition::ID = 0;

// Values are redefinable if they're integers and not constants.
static bool IsRedefinable(Value *V) {
  return V->getType()->isIntegerTy() && !isa<Constant>(V);
}

static PHINode *CreateNamedPhi(Value *V, Twine Prefix,
                               BasicBlock::iterator Position) {
  const auto Name = (V->hasName() ? Prefix + "." + V->getName() : Prefix).str();
  return PHINode::Create(V->getType(), 1, Name, Position);
}

void Redefinition::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<DominanceFrontier>();
  AU.addPreserved<SAGEInterface>();
  AU.addPreserved<PythonInterface>();
  AU.addPreserved<LoopInfoPass>();
  AU.setPreservesCFG();
}

bool Redefinition::runOnFunction(Function &F) {
  DT_  = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  DF_  = &getAnalysis<DominanceFrontier>();

  createSigmasInFunction(&F);

  for (auto &BB : F)
    for (auto &I : BB)
      if (PHINode *Phi = dyn_cast<PHINode>(&I))
        if (Phi->getNumIncomingValues() == 1)
          Redef_[&BB][Phi->getIncomingValue(0)] = Phi;

  return true;
}

PHINode *Redefinition::getRedef(Value *V, BasicBlock *BB) {
  auto BBIt = Redef_.find(BB);
  if (BBIt == Redef_.end())
    return nullptr;
  auto Map = BBIt->second.find(V);
  return Map != BBIt->second.end() ? Map->second : nullptr;
}

// Create sigma nodes for all branches in the function.
void Redefinition::createSigmasInFunction(Function *F) {
  for (auto& BB : *F) {
    // Rename operands used in conditional branches and their dependencies.
    TerminatorInst *TI = BB.getTerminator();
    if (BranchInst *BI = dyn_cast<BranchInst>(TI))
      if (BI->isConditional())
        createSigmasForCondBranch(BI);
  }
}

void Redefinition::createSigmasForCondBranch(BranchInst *BI) {
  assert(BI->isConditional() && "Expected conditional branch");

  ICmpInst *ICI = dyn_cast<ICmpInst>(BI->getCondition());
  if (!ICI || !ICI->getOperand(0)->getType()->isIntegerTy())
    return;

  DEBUG(dbgs() << "createSigmasForCondBranch: " << *BI << "\n");

  Value *Left = ICI->getOperand(0);
  Value *Right = ICI->getOperand(1);

  BasicBlock *TB = BI->getSuccessor(0);
  BasicBlock *FB = BI->getSuccessor(1);

  bool HasSinglePredTB = TB->getSinglePredecessor() != nullptr;
  bool HasSinglePredFB = FB->getSinglePredecessor() != nullptr;
  bool IsRedefinableRight = IsRedefinable(Right);

  if (IsRedefinable(Left)) {
    // We don't want to place extranius definitions of a value, so only
    // place the sigma once if the branch operands are the same.
    Value *Second = Left != Right && IsRedefinableRight ? Right : nullptr;
    if (HasSinglePredTB)
      createSigmaNodesForValueAt(Left, Second, TB);
    if (HasSinglePredFB)
      createSigmaNodesForValueAt(Left, Second, FB);
  }

  if (IsRedefinableRight) {
    if (HasSinglePredTB)
      createSigmaNodesForValueAt(Right, nullptr, TB);
    if (HasSinglePredFB)
      createSigmaNodesForValueAt(Right, nullptr, FB);
  }
}

// Creates sigma nodes for the value and the transitive closure of its
// dependencies.
// To avoid extra redefinitions, we pass in both branch values so that the
// and use the union of both redefinition sets.
void Redefinition::createSigmaNodesForValueAt(Value *V, Value *C,
                                              BasicBlock *BB) {
  assert(BB->getSinglePredecessor() && "Block has multiple predecessors");

  DEBUG(
    dbgs() << "createSigmaNodesForValueAt: " << *V;
    if (C) dbgs() << " and " << *C;
    dbgs() << " at " << BB->getName() << "\n";
  );

  auto Position = BB->getFirstInsertionPt();
  if (IsRedefinable(V) && dominatesUse(V, BB))
     createSigmaNodeForValueAt(V, BB, Position);
  if (C && IsRedefinable(C) && dominatesUse(C, BB))
     createSigmaNodeForValueAt(C, BB, Position);
}

void Redefinition::createSigmaNodeForValueAt(Value *V, BasicBlock *BB,
                                             BasicBlock::iterator Position) {
  DEBUG(dbgs() << "createSigmaNodeForValueAt: " << *V << "\n");

  auto I = BB->begin();
  while (!isa<PHINode>(&(*I)) && I != BB->end()) ++I;
  while (isa<PHINode>(&(*I)) && I != BB->end()) {
    PHINode *Phi = cast<PHINode>(&(*I));
    if (Phi->getNumIncomingValues() == 1 &&
        Phi->getIncomingValue(0) == V)
      return;
    ++I;
  }

  PHINode *BranchRedef = CreateNamedPhi(V, GetRedefPrefix(), Position);
  BranchRedef->addIncoming(V, BB->getSinglePredecessor());
  NumCreatedSigmas++;

  std::list<PHINode*> FrontierRedefs;
  // Phi nodes should be created on all blocks in the dominance frontier of BB
  // where V is defined.
  auto DI = DF_->find(BB);
  if (DI != DF_->end())
    for (auto& BI : DI->second)
      // If the block in the frontier dominates a use of V, then a phi node
      // should be created at said block.
      if (dominatesUse(V, BI))
         if (PHINode *FrontierRedef = createPhiNodeAt(V, BI)) {
           FrontierRedefs.push_front(FrontierRedef);
           // Replace all incoming definitions with the omega node for every
           // predecessor where the omega node is defined.
           for (auto PI = pred_begin(BI), PE = pred_end(BI); PI != PE; ++PI) {
             if (DT_->dominates(BB, *PI)) {
               FrontierRedef->removeIncomingValue(*PI);
               FrontierRedef->addIncoming(BranchRedef, *PI);
             }
           }
         }

  // Replace all users of the V with the new sigma, starting at BB.
  replaceUsesOfWithAfter(V, BranchRedef, BB);

  // TODO: we should probably do replacing in reverse postorder.
  for (auto &FrontierRedef : FrontierRedefs) {
    replaceUsesOfWithAfter(FrontierRedef, BranchRedef, BB);
  }
}

// Creates a phi node for the given value at the given block.
PHINode *Redefinition::createPhiNodeAt(Value *V, BasicBlock *BB) {
  assert(!V->getType()->isPointerTy() && "Value must not be a pointer");

  DEBUG(dbgs() << "createPhiNodeAt: " << *V << " at "
               << BB->getName() << "\n");

  // Return null if V isn't defined on all predecessors of BB.
  if (Instruction *I = dyn_cast<Instruction>(V))
    for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB); PI != PE; ++PI)
      if (!DT_->dominates(I->getParent(), *PI))
        return nullptr;

  PHINode *Phi = CreateNamedPhi(V, GetPhiPrefix(), BB->begin());

  // Add the default incoming values.
  for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB); PI != PE; ++PI)
    Phi->addIncoming(V, *PI);

  // Replace all uses of V with the phi node, starting at BB.
  replaceUsesOfWithAfter(V, Phi, BB);

  NumCreatedFrontierPhis++;

  return Phi;
}

// Returns true if BB dominates a use of V.
bool Redefinition::dominatesUse(Value *V, BasicBlock *BB) {
  for (auto UI = V->user_begin(), UE = V->user_end(); UI != UE; ++UI)
    // Disregard phi nodes, since they can dominate their operands.
    if (isa<PHINode>(*UI) || V == *UI)
      continue;
    else if (Instruction *I = dyn_cast<Instruction>(*UI))
      if (DT_->dominates(BB, I->getParent()))
        return true;
  return false;
}

void Redefinition::replaceUsesOfWithAfter(Value *V, Value *R, BasicBlock *BB) {
  DEBUG(dbgs() << "Redefinition: replaceUsesOfWithAfter: " << *V << " to "
               << *R << " after " << BB->getName() << "\n");

  std::set<Instruction*> Replace;
  for (auto UI = V->user_begin(), UE = V->user_end(); UI != UE; ++UI)
    if (Instruction *I = dyn_cast<Instruction>(*UI))
      Replace.insert(I);

  for (auto& I : Replace) {
      // If the instruction's parent dominates BB, mark the instruction to
      // be replaced.
      if (I != R && DT_->dominates(BB, I->getParent()))
        I->replaceUsesOfWith(V, R);
      // If the parent does not dominate BB, check if the use is a phi and
      // replace the incoming value later on.
      else if (PHINode *Phi = dyn_cast<PHINode>(I))
        for (unsigned Idx = 0; Idx < Phi->getNumIncomingValues(); ++Idx)
          if (Phi->getIncomingValue(Idx) == V &&
              DT_->dominates(BB, Phi->getIncomingBlock(Idx)))
            Phi->setIncomingValue(Idx, R);
  }
}

