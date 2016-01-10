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

static cl::opt<bool>
    ShouldUseSymBounds("sra-use-sym-bounds", cl::init(false), cl::Hidden,
        cl::desc("Use symbolic mins & maxes for integer bounds"));

static cl::opt<int>
    MaxPhiEvalSize("sra-max-phi-eval-size", cl::init(-1), cl::Hidden,
        cl::desc("Maximum number of operands on phi nodes, phi nodes which"
            "have more will not be evaluated"));

static cl::opt<int>
    MaxExprSize("sra-max-expr-size", cl::init(8), cl::Hidden,
        cl::desc("Maximum number of (recursive) arguments to min/max"
            " expressions before they're widened to -oo/+oo"));

static cl::opt<bool>
    UseNumericBounds("sra-use-numeric-bounds", cl::init(false), cl::Hidden,
        cl::desc("Use numbers as bounds, instead of -/+oo"));

const unsigned CHANGED_LOWER = 1 << 0;
const unsigned CHANGED_UPPER = 1 << 1;

static SAGERange GetBoundsForTy(Type *Ty, SAGEInterface *SI) {
  static SAGERange InfRange =
      SAGERange(SAGEExpr::getMinusInf(*SI), SAGEExpr::getPlusInf(*SI));
  if (!UseNumericBounds) {
    return InfRange;
  }

  unsigned Width = Ty->getIntegerBitWidth();
  if (ShouldUseSymBounds) {
    switch (Width) {
      case 8:
        return SAGERange(SAGEExpr(*SI, "CHAR_MIN"), SAGEExpr(*SI, "UCHAR_MAX"));
      case 16:
        return SAGERange(SAGEExpr(*SI, "SHRT_MIN"), SAGEExpr(*SI, "USHRT_MAX"));
      case 32:
        return SAGERange(SAGEExpr(*SI, "INT_MIN"), SAGEExpr(*SI, "UINT_MAX"));
      case 64:
        return SAGERange(SAGEExpr(*SI, "LONG_MIN"), SAGEExpr(*SI, "ULONG_MAX"));
    }
  }
  uint64_t Upper = APInt::getMaxValue(Width).getZExtValue();
  int64_t Lower = APInt::getSignedMinValue(Width).getSExtValue();
  return SAGERange(SAGEExpr(*SI, Lower), SAGEExpr(*SI, Upper));
}

static SAGERange GetBoundsForValue(Value *V, SAGEInterface *SI) {
  return GetBoundsForTy(V->getType(), SI);
}

static SAGERange BinaryOp(BinaryOperator *BO, SymbolicRangeAnalysis *SRA) {
  DEBUG(dbgs() << "SRA: BinaryOp: " << *BO << "\n");

  auto LHS = SRA->getStateOrInf(BO->getOperand(0)),
       RHS = SRA->getStateOrInf(BO->getOperand(1));

  switch (BO->getOpcode()) {
    case Instruction::Add: {
      DEBUG(dbgs() << "     BinaryOp: " << LHS << " + " << RHS << "\n");
      auto Ret = LHS + RHS;
      DEBUG(dbgs() << "     BinaryOp: return " << Ret << "\n");
      return Ret;
    }
    case Instruction::Sub: {
      DEBUG(dbgs() << "     BinaryOp: " << LHS << " - " << RHS << "\n");
      auto Ret = LHS - RHS;
      DEBUG(dbgs() << "     BinaryOp: return " << Ret << "\n");
      return Ret;
    }
    case Instruction::Mul: {
      DEBUG(dbgs() << "     BinaryOp: " << LHS << " * " << RHS << "\n");

      bool boundsShouldBeInf = LHS.getLower().isMinusInf()
          || RHS.getLower().isMinusInf() || LHS.getUpper().isPlusInf()
          || RHS.getUpper().isPlusInf();
      if (boundsShouldBeInf) {
        auto Ret = GetBoundsForValue(BO, &SRA->getSI());
        DEBUG(dbgs() << "     BinaryOp: return " << Ret << "\n");
        return Ret;
      }

      auto Ret = LHS * RHS;
      DEBUG(dbgs() << "     BinaryOp: return " << Ret << "\n");
      return Ret;
    }
    case Instruction::SDiv:
    case Instruction::UDiv: {
      DEBUG(dbgs() << "     BinaryOp: " << LHS << "/" << RHS << "\n");

      bool boundsShouldBeInf = LHS.getLower().isMinusInf()
          || RHS.getLower().isMinusInf() || LHS.getUpper().isPlusInf()
          || RHS.getUpper().isPlusInf();
      if (boundsShouldBeInf) {
        auto Ret = GetBoundsForValue(BO, &SRA->getSI());
        DEBUG(dbgs() << "     BinaryOp: return " << Ret << "\n");
        return Ret;
      }

      auto Ret = LHS/RHS;
      DEBUG(dbgs() << "     BinaryOp: return " << Ret << "\n");
      return Ret;
    }
    default: {
      auto Ret = GetBoundsForValue(BO, &SRA->getSI());
      DEBUG(dbgs() << "     BinaryOp: return " << Ret << "\n");
      return Ret;
    }
  }
}

static SAGERange Narrow(PHINode *Phi, Value *V, ICmpInst::Predicate Pred,
                        SymbolicRangeAnalysis *SRA) {
  DEBUG(dbgs() << "SRA: Narrow: " << *Phi << ", " << *V << "\n");

  auto Ret   = SRA->getStateOrInf(Phi->getIncomingValue(0)),
       Bound = SRA->getStateOrInf(V);

  switch (Pred) {
    case CmpInst::ICMP_SLT:
    case CmpInst::ICMP_ULT:
      DEBUG(dbgs() << "     Narrow: " << Ret << " < " << Bound << "\n");
      Ret.setUpper(Bound.getUpper() - 1);
      break;
    case CmpInst::ICMP_SLE:
    case CmpInst::ICMP_ULE:
      DEBUG(dbgs() << "     Narrow: " << Ret << " <= " << Bound << "\n");
      Ret.setUpper(Bound.getUpper());
      break;
    case CmpInst::ICMP_SGT:
    case CmpInst::ICMP_UGT:
      DEBUG(dbgs() << "     Narrow: " << Ret << " > " << Bound << "\n");
      Ret.setLower(Bound.getLower() + 1);
      break;
    case CmpInst::ICMP_SGE:
    case CmpInst::ICMP_UGE:
      DEBUG(dbgs() << "     Narrow: " << Ret << " >= " << Bound << "\n");
      Ret.setLower(Bound.getLower());
      break;
    case CmpInst::ICMP_EQ:
      DEBUG(dbgs() << "     Narrow: " << Ret << " = " << Bound << "\n");
      Ret = Bound;
      break;
    case CmpInst::ICMP_NE:
      if (SRA->hasStableLowerBound(Phi)) {
        DEBUG(dbgs() << "     Narrow: " << Ret << " != " << Bound
            << " (lower)\n");
        Ret.setUpper(Bound.getUpper() - 1);
      } else if (SRA->hasStableUpperBound(Phi)) {
        DEBUG(dbgs() << "     Narrow: " << Ret << " != " << Bound
            << " (upper)\n");
        Ret.setLower(Bound.getLower() + 1);
      }
      break;
    default:
      break;
  }

  DEBUG(dbgs() << "     Narrow: return " << Ret << "\n");
  return Ret;
}

static SAGERange Meet(PHINode *Phi, SymbolicRangeAnalysis *SRA) {
  DEBUG(dbgs() << "SRA: Meet: " << *Phi << "\n");

  if (MaxPhiEvalSize > 0 && Phi->getNumOperands() > (unsigned) MaxPhiEvalSize) {
    SAGERange Ret =
        GetBoundsForTy(cast<IntegerType>(Phi->getType()), &SRA->getSI());
    Ret.setLower(Ret.getLower());
    Ret.setUpper(Ret.getUpper());
    DEBUG(dbgs() << "     Meet: pruning evaluation\n");
    DEBUG(dbgs() << "     Meet: return " << Ret << "\n");
    return Ret;
  }

  SAGERange Ret = SRA->getState(Phi->getIncomingValue(0));
  auto OI = Phi->op_begin(), OE = Phi->op_end();
  for (; Ret == SRA->getBottom() && OI != OE; ++OI)
    Ret = SRA->getState(*OI);

  DEBUG(dbgs() << "     Meet: starting with " << Ret << "\n");

  for (; OI != OE; ++OI) {
    SAGERange Incoming = SRA->getState(*OI);
    if (Incoming == SRA->getBottom())
      continue;
    Ret.setLower(Ret.getLower().min(Incoming.getLower()));
    Ret.setUpper(Ret.getUpper().max(Incoming.getUpper()));
    DEBUG(dbgs() << "     Meet: meet " << Ret << " and " << Incoming << "\n");
  }

  DEBUG(dbgs() << "     Meet: return " << Ret << "\n");
  return Ret;
}

void SymbolicRangeAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<SAGEInterface>();
  AU.addRequired<Redefinition>();
  AU.setPreservesAll();
}

bool SymbolicRangeAnalysis::runOnFunction(Function& F) {
  Module_ = F.getParent();
  SI_ = &getAnalysis<SAGEInterface>();

  dbgs() << "SRA: runOnModule: " << F.getName() << "\n";

  RDF_ = &getAnalysis<Redefinition>();

  initialize(&F);
  reset(&F);
  iterate(&F);
  reset(&F);
  iterate(&F);
  reset(&F);
  iterate(&F);
  widen(&F);

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
  } else
    return (F->getName() + Twine("_") + Twine(Temp++)).str();
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

  auto Bounds = GetBoundsForValue(V, SI_);
  if (Range.getLower().getSize() > MaxExprSize) {
    Range.setLower(Bounds.getLower());
  }

  if (Range.getUpper().getSize() > MaxExprSize) {
    Range.setUpper(Bounds.getUpper());
  }

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
  unsigned Changed = (Prev.getLower().isNE(New.getLower()) ? CHANGED_LOWER : 0)
      | (Prev.getUpper().isNE(New.getUpper()) ? CHANGED_UPPER : 0);
  Changed_[V] = Changed;

  auto It = StableBounds_.find(V);
  if (It == StableBounds_.end()) {
    StableBounds_[V] = std::make_pair(true, true);
    return;
  }

  bool StableLower = !(Changed & CHANGED_LOWER),
      StableUpper = !(Changed & CHANGED_UPPER);
  It->second = std::make_pair(
      It->second.first & StableLower, It->second.second & StableUpper);
}

bool SymbolicRangeAnalysis::hasStableLowerBound(Value *V) const {
  auto It = StableBounds_.find(V);
  return It == StableBounds_.end() ? false : It->second.first;
}

bool SymbolicRangeAnalysis::hasStableUpperBound(Value *V) const {
  auto It = StableBounds_.find(V);
  return It == StableBounds_.end() ? false : It->second.second;
}

SAGERange SymbolicRangeAnalysis::getState(Value *V) const {
  // TODO: Handle ptrtoint.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
    return SAGEExpr(*SI_, CI->getValue().getSExtValue());
  if (isa<UndefValue>(V) || isa<Constant>(V))
    return GetBoundsForValue(V, SI_);
  auto It = State_.find(V);
  assert(It != State_.end() && "Requested value is not in map");
  return It->second;
}

SAGERange SymbolicRangeAnalysis::getStateOrInf(Value *V) const {
  auto State = getState(V);
  return State != getBottom()
      ? State : GetBoundsForTy(cast<IntegerType>(V->getType()), SI_);
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
  setName(I,  makeName(I->getParent()->getParent(), I));
  //  setState(I, GetBoundsForValue(I, SI_));
  if (isa<LoadInst>(I))
    setState(I, SAGEExpr(*SI_, getName(I).c_str()));
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
    auto Next = Worklist_.begin();
    Instruction *I = Next->second;
    Worklist_.erase(Next);
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
          auto Bounds = GetBoundsForValue(&I, SI_);
          if (Changed_[&I] & CHANGED_LOWER)
            State.setLower(Bounds.getLower());
          if (Changed_[&I] & CHANGED_UPPER)
            State.setUpper(Bounds.getUpper());
          setState(&I, State);
        }
}

void SymbolicRangeAnalysis::print(raw_ostream &OS, const Module*) const {
  for (auto P : State_)
    OS << "[[" << getName(P.first) << "]] = " << P.second << "\n";
}

