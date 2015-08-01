//===---------------------- SymbolicRangeAnalysis.cpp ---------------------===//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sra"

#include "SymbolicRangeAnalysis.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

raw_ostream& operator<<(raw_ostream& OS, const SymbolicRangeAnalysis& SRR) {
  SRR.print(OS, nullptr);
  return OS;
}

using namespace llvm;

static RegisterPass<SymbolicRangeAnalysis>
  X("sra", "Symbolic range analysis with SAGE and QEPCAD");
char SymbolicRangeAnalysis::ID = 0;

const unsigned CHANGED_LOWER = 1 << 0;
const unsigned CHANGED_UPPER = 1 << 1;

static SAGERange BinaryOp(BinaryOperator *BO, SymbolicRangeAnalysis *SRA) {
  DEBUG(dbgs() << "SRA: BinaryOp: " << *BO << "\n");

  auto LHS = SRA->getStateOrInf(BO->getOperand(0)),
       RHS = SRA->getStateOrInf(BO->getOperand(1));

  switch (BO->getOpcode()) {
    case Instruction::Add:
      return LHS + RHS;
    case Instruction::Sub:
      return LHS - RHS;
    case Instruction::Mul:
      return LHS * RHS;
    case Instruction::SDiv:
    case Instruction::UDiv:
      return LHS/RHS;
    default:
      return SAGERange(SAGEExpr::getMinusInf(SRA->getSI()),
                       SAGEExpr::getPlusInf(SRA->getSI()));
  }
}

static SAGERange Narrow(PHINode *Phi, Value *V, ICmpInst::Predicate Pred,
                        SymbolicRangeAnalysis *SRA) {
  DEBUG(dbgs() << "SRA: Narrow: " << *Phi << ", " << *V << "\n");

  auto Ret   = SRA->getStateOrInf(Phi->getIncomingValue(0)),
       Bound = SRA->getState(V);

  std::set<SAGEExpr> Set;
  switch (Pred) {
    case CmpInst::ICMP_SLT:
    case CmpInst::ICMP_ULT:
      Ret.setUpper(Bound.getUpper() - 1);
      break;
    case CmpInst::ICMP_SLE:
    case CmpInst::ICMP_ULE:
      Ret.setUpper(Bound.getUpper());
      break;
    case CmpInst::ICMP_SGT:
    case CmpInst::ICMP_UGT:
      Ret.setLower(Bound.getLower() + 1);
      break;
    case CmpInst::ICMP_SGE:
    case CmpInst::ICMP_UGE:
      Ret.setLower(Bound.getLower());
      break;
    case CmpInst::ICMP_EQ:
      Ret = Bound;
      break;
    default:
      break;
  }
  return Ret;
}

static SAGERange Meet(PHINode *Phi, SymbolicRangeAnalysis *SRA) {
  DEBUG(dbgs() << "SRA: Meet: " << *Phi << "\n");

  SAGERange Ret = SRA->getState(Phi->getIncomingValue(0));
  auto OI = Phi->op_begin(), OE = Phi->op_end();
  for (; Ret == SRA->getBottom() && OI != OE; ++OI)
    Ret = SRA->getState(*OI);

  for (; OI != OE; ++OI) {
    SAGERange Incoming = SRA->getState(*OI);
    if (Incoming == SRA->getBottom())
      continue;
    Ret.setLower(Ret.getLower().min(Incoming.getLower()));
    Ret.setUpper(Ret.getUpper().max(Incoming.getUpper()));
  }
  return Ret;
}

void SymbolicRangeAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<SAGEInterface>();
  AU.addRequired<Redefinition>();
  AU.setPreservesAll();
}

bool SymbolicRangeAnalysis::runOnModule(Module& M) {
  Module_ = &M;
  SI_ = &getAnalysis<SAGEInterface>();

  for (auto &F : M) {
    if (F.isIntrinsic() || F.isDeclaration())
      continue;

    dbgs() << "SRA: runOnModule: " << F.getName() << "\n";

    RDF_ = &getAnalysis<Redefinition>(F);

    initialize(&F);
    reset(&F);
    iterate(&F);
    reset(&F);
    iterate(&F);
    reset(&F);
    iterate(&F);
    widen(&F);
  }

  DEBUG(dbgs() << *this << "\n");

  return false;
}

SAGEExpr SymbolicRangeAnalysis::getBottomExpr() const {
  static SAGEExpr BottomExpr(*SI_, "_BOT_");
  return BottomExpr;
}

SAGERange SymbolicRangeAnalysis::getBottom() const {
  static SAGERange BottomRange(getBottomExpr());
  return BottomRange;
}

std::string SymbolicRangeAnalysis::makeName(Function *F, Value *V) {
  static unsigned Temp = 1;
  if (V->hasName()) {
    auto Name = (F->getName() + Twine("_") + V->getName()).str();
    std::replace(Name.begin(), Name.end(), '.', '_');
    return Name;
  } else {
    auto Name = F->getName() + Twine("_") + Twine(Temp++);
    return Name.str();
  }
}

void SymbolicRangeAnalysis::setName(Value *V, std::string Name) {
  Name_[V] = Name;
  Value_[Name] = V;
}

std::string SymbolicRangeAnalysis::getName(Value *V) const {
  auto It = Name_.find(V);
  assert(It != Name_.end() && "Requested value is not in map");
  return It->second;
}

void SymbolicRangeAnalysis::setState(Value *V, SAGERange Range) {
  DEBUG(dbgs() << "SRA: setState(" << *V << "," << Range << ")\n");
  if (Range.getLower().isEQ(SAGEExpr::getNaN(*SI_)))
    Range.setLower(SAGEExpr::getMinusInf(*SI_));
  if (Range.getUpper().isEQ(SAGEExpr::getNaN(*SI_)))
    Range.setUpper(SAGEExpr::getPlusInf(*SI_));
  auto It = State_.insert(std::make_pair(V, Range));
  if (!It.second) {
    if (It.first->second != Range)
      setChanged(V, It.first->second, Range);
    It.first->second = Range;
  } else if (isa<Instruction>(V)) {
    Changed_[V] = CHANGED_LOWER | CHANGED_UPPER;
  }
}

void SymbolicRangeAnalysis::setChanged(Value *V, SAGERange &Prev,
                                       SAGERange &New) {
  Changed_[V] = (Prev.getLower().isNE(New.getLower()) ? CHANGED_LOWER : 0) |
                (Prev.getUpper().isNE(New.getUpper()) ? CHANGED_UPPER : 0);
}

SAGERange SymbolicRangeAnalysis::getState(Value *V) const {
  // TODO: Handle ptrtoint.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
    return SAGEExpr(*SI_, CI->getValue().getSExtValue());
  if (isa<UndefValue>(V) || isa<Constant>(V))
    return SAGERange(SAGEExpr::getMinusInf(*SI_), SAGEExpr::getPlusInf(*SI_));
  auto It = State_.find(V);
  assert(It != State_.end() && "Requested value is not in map");
  return It->second;
}

SAGERange SymbolicRangeAnalysis::getStateOrInf(Value *V) const {
  static SAGERange Inf(SAGEExpr::getMinusInf(*SI_), SAGEExpr::getPlusInf(*SI_));
  auto State = getState(V);
  return State != getBottom() ? State : Inf;
}

std::pair<Value*, Value*>
    SymbolicRangeAnalysis::getRangeValuesFor(Value *V, IRBuilder<> IRB) const {
  SAGERange Range = getStateOrInf(V);
  IntegerType *Ty = cast<IntegerType>(V->getType());
  Value *Lower = Range.getLower().toValue(Ty, IRB, Value_, Module_),
        *Upper = Range.getUpper().toValue(Ty, IRB, Value_, Module_);
  return std::make_pair(Lower, Upper);
}

void SymbolicRangeAnalysis::createNarrowingFn(Value *LHS, Value *RHS,
                                      CmpInst::Predicate Pred, BasicBlock *BB) {
  if (auto Redef = RDF_->getRedef(LHS, BB))
    Fn_[Redef] = [Redef, RHS, Pred, this] ()
      { return Narrow(Redef, RHS, Pred, this); };
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
  static SAGERange Inf(SAGEExpr::getMinusInf(*SI_), SAGEExpr::getPlusInf(*SI_));
  setName(I,  makeName(I->getParent()->getParent(), I));
  if (isa<LoadInst>(I))
    setState(I, Inf);
  else
    setState(I, getBottom());
  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::SDiv:
    case Instruction::UDiv:
      Fn_[I] = [I, this] ()
        { return BinaryOp(cast<BinaryOperator>(I), this); };
      break;
    case Instruction::PHI:
      if (!Fn_.count(I))
        Fn_[I] = [I, this] ()
          { return Meet(cast<PHINode>(I), this); };
      break;
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
        Fn_[I] = [I, this] ()
          { return this->getState(*I->op_begin()); };
      break;
    default:
      return;
  }
}

void SymbolicRangeAnalysis::initialize(Function *F) {
  // Create symbols for the function's integer arguments.
  for (auto AI = F->arg_begin(), AE = F->arg_end(); AI != AE; ++AI)
    if (AI->getType()->isIntegerTy()) {
      std::string Name = makeName(F, &(*AI));
      setName(&(*AI), Name);
      SAGEExpr Arg(*SI_, Name.c_str());
      // Range is symbolic - [Arg, Arg].
      setState(&(*AI), SAGERange(Arg));
    }

  // Create a closure for each instruction.
  for (auto &BB : *F) {
    // Handle sigma nodes.
    TerminatorInst *TI = BB.getTerminator();
    if (BranchInst *BI = dyn_cast<BranchInst>(TI))
      if (BI->isConditional())
        if (ICmpInst *ICI = dyn_cast<ICmpInst>(BI->getCondition()))
          handleBranch(BI, ICI);

    // Handle everything that's not a sigma node.
    for (auto &I : BB)
      if (I.getType()->isIntegerTy()) {
        Mapping_[&I] = Mapping_.size() + 1;
        handleIntInst(&I);
      }
  }
}

void SymbolicRangeAnalysis::reset(Function *F) {
  for (auto &BB : *F)
    for (auto &I : BB)
      if (Changed_.count(&I) && Changed_[&I])
        Worklist_.insert(std::make_pair(Mapping_[&I], &I));

  Evaled_.clear();
  Changed_.clear();
}

void SymbolicRangeAnalysis::iterate(Function *F) {
  DEBUG(dbgs() << "SRA: Iterate\n");
  while (!Worklist_.empty()) {
    auto Next = Worklist_.begin(); Worklist_.erase(Next);
    Instruction *I = Next->second;
    if (Fn_.count(I) && !Evaled_.count(I)) {
      Evaled_.insert(I);
      setState(I, Fn_[I]());
      for (auto UI = I->use_begin(), UE = I->use_end(); UI != UE; ++UI)
        if (Instruction *Use = dyn_cast<Instruction>(*UI))
          if (!Evaled_.count(Use))
            Worklist_.insert(std::make_pair(Mapping_[Use], Use));
    }
  }
}

void SymbolicRangeAnalysis::widen(Function *F) {
  DEBUG(dbgs() << "SRA: Widen\n");
  for (auto &BB : *F)
    for (auto &I : BB)
      if (I.getType()->isIntegerTy())
        if (Changed_.count(&I) && Changed_[&I]) {
          auto State = getStateOrInf(&I);
          if (Changed_[&I] & CHANGED_LOWER)
            State.setLower(SAGEExpr::getMinusInf(*SI_));
          if (Changed_[&I] & CHANGED_UPPER)
            State.setUpper(SAGEExpr::getPlusInf(*SI_));
          setState(&I, State);
        }
}

void SymbolicRangeAnalysis::print(raw_ostream &OS, const Module*) const {
  for (auto P : State_)
    OS << "[[" << getName(P.first) << "]] = " << P.second << "\n";
}

