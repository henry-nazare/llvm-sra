#define DEBUG_TYPE "sra-graph"

#include "RAGraphBase.h"

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Support/Debug.h"

using namespace llvm;
using llvmpy::Get;

namespace {

struct SRAGraphObjInfo : public PythonObjInfo {
  SRAGraphObjInfo(const char *Fn)
      : PythonObjInfo("llvmsra.graph", "SRAGraph", Fn) {}
};

} // end anonymous namespace

template <typename T>
static iterator_range<T> Range(T begin, T end) {
  return iterator_range<T>(begin, end);
}

static std::map<unsigned, const char*> HandledBinaryOperators = {
  {Instruction::Add, "add"}, {Instruction::Sub, "sub"},
  {Instruction::Mul, "mul"}, {Instruction::SDiv, "div"},
  {Instruction::UDiv, "div"}
};

static std::map<CmpInst::Predicate, const char*> HandledCmpPredicates = {
  {ICmpInst::ICMP_SLT, "lt"}, {ICmpInst::ICMP_ULT, "lt"},
  {ICmpInst::ICMP_SLE, "le"}, {ICmpInst::ICMP_ULE, "le"},
  {ICmpInst::ICMP_SGT, "gt"}, {ICmpInst::ICMP_UGT, "gt"},
  {ICmpInst::ICMP_SGE, "ge"}, {ICmpInst::ICMP_UGE, "ge"}
};

static std::map<CmpInst::Predicate, CmpInst::Predicate>
    SwappedInversePredicate = {
  {ICmpInst::ICMP_SLT, ICmpInst::ICMP_SLE},
  {ICmpInst::ICMP_SLE, ICmpInst::ICMP_SLT},
  {ICmpInst::ICMP_SGT, ICmpInst::ICMP_SGE},
  {ICmpInst::ICMP_SGE, ICmpInst::ICMP_SGT},
  {ICmpInst::ICMP_EQ, ICmpInst::ICMP_EQ},
  {ICmpInst::ICMP_NE, ICmpInst::ICMP_NE}
};

bool IsSigmaNode(Instruction *I, Redefinition &RDF) {
  PHINode *Phi = dyn_cast<PHINode>(I);
  return Phi && Phi->getNumIncomingValues() == 1 &&
      RDF.getRedef(Phi->getIncomingValue(0), Phi->getParent()) == Phi;
}

std::pair<CmpInst::Predicate, Value*>
    GetSigmaBound(PHINode *Phi, Redefinition &RDF) {
  assert(IsSigmaNode(Phi, RDF)
      && "GetSigmaBound() called on non-sigma phi node");

  BasicBlock *BB = Phi->getParent();
  BranchInst *BI =
      cast<BranchInst>(BB->getSinglePredecessor()->getTerminator());
  ICmpInst *ICI = cast<ICmpInst>(BI->getCondition());

  Value *LHS = ICI->getOperand(0), *RHS = ICI->getOperand(1);
  Value *Incoming = Phi->getOperand(0);

  assert(BI->getSuccessor(0) == BB || BI->getSuccessor(1) == BB);
  assert(ICI->getOperand(0) == Incoming || ICI->getOperand(1) == Incoming);

  if (BI->getSuccessor(0) == BB) {
    return Incoming == LHS
        ? std::make_pair(ICI->getPredicate(), RHS)
        : std::make_pair(ICI->getSwappedPredicate(), LHS);
  }

  return Incoming == LHS
      ? std::make_pair(ICI->getInversePredicate(), RHS)
      : std::make_pair(SwappedInversePredicate.at(ICI->getPredicate()), LHS);
}

static SRAGraphObjInfo graph_SRAGraph(nullptr);
RAGraphBase::RAGraphBase(
    Function *F, Redefinition &RDF, SAGENameVault &SNV)
        : SAGEAnalysisGraph(graph_SRAGraph({}), SNV), F_(F), RDF_(RDF) {
}

SAGERange RAGraphBase::getRange(Value *V) const {
  static PythonAttrInfo graph_Node_state("state");
  auto It = Node_.find(V);
  assert(It != Node_.end() && "Requested value not in map");
  return graph_Node_state.get(It->second);
}

void RAGraphBase::initialize() {
  initializeArguments();
  initializeIntInsts();
  initializeIncoming();
}

void RAGraphBase::initializeArguments() {
  for (auto &A : F_->args()) {
    if (A.getType()->isIntegerTy()) {
      addValue(&A);
    }
  }
}

void RAGraphBase::initializeIntInsts() {
  ReversePostOrderTraversal<Function*> RPOT(F_);
  for (auto &BB : Range(RPOT.begin(), RPOT.end())) {
    for (auto &I : *BB) {
      if (I.getType()->isIntegerTy()) {
        addIntInst(&I);
      }
    }
  }
}

void RAGraphBase::initializeIncoming() {
  for (auto &I : NodesWithIncoming_) {
    addIncoming(I->operands(), I);
    if (IsSigmaNode(I, RDF_)) {
      auto Bound = GetSigmaBound(cast<PHINode>(I), RDF_);
      addIncoming(Bound.second, I);
    }
  }
}

void RAGraphBase::addIntInst(Instruction *I) {
  if (HandledBinaryOperators.count(I->getOpcode())) {
    return (void) addBinOp(cast<BinaryOperator>(I));
  }

  if (PHINode *Phi = dyn_cast<PHINode>(I)) {
    if (IsSigmaNode(Phi, RDF_)) {
      auto Bound = GetSigmaBound(Phi, RDF_);
      return (void) addSigmaNode(Phi, Bound.first);
    }
    return (void) addPhiNode(Phi);
  }

  addValue(I);
}

PyObject *RAGraphBase::getNode(Value *V) {
  assert(V->getType()->isIntegerTy() && "Value is not an integer");
  auto It = Node_.find(V);
  if (It != Node_.end()) {
    return It->second;
  }

  // TODO: we also want to handle other constants, such as UndefValue.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    PyObject *Node = getConstant(Get(CI->getValue()));
    Node_[CI] = Node;
    return Node;
  } else if (isa<Constant>(V)) {
    PyObject *Node = getConstant(getNodeName(V));
    Node_[V] = Node;
    return Node;
  }

  assert(false && "Requested value not in node map");
}

PyObject *RAGraphBase::getNodeName(Value *V) const {
  // TODO: we also want to handle other constants, such as UndefValue.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    return Get(CI->getValue());
  }

  return Get(SNV_.getName(V));
}

void RAGraphBase::addBinOp(BinaryOperator *BO) {
  auto It = HandledBinaryOperators.find(BO->getOpcode());
  assert(It != HandledBinaryOperators.end() && "Uhandled predicate");
  NodesWithIncoming_.insert(BO);
  setNode(BO, getBinOp(getNodeName(BO), It->second));
}

void RAGraphBase::addPhiNode(PHINode *Phi) {
  NodesWithIncoming_.insert(Phi);
  setNode(Phi, getPhi(getNodeName(Phi)));
}

void RAGraphBase::addSigmaNode(PHINode *Sigma, CmpInst::Predicate Pred) {
  auto It = HandledCmpPredicates.find(Pred);
  assert(It != HandledCmpPredicates.end() && "Uhandled predicate");
  NodesWithIncoming_.insert(Sigma);
  setNode(Sigma, getSigma(getNodeName(Sigma), It->second));
}

PyObject *RAGraphBase::getBinOp(PyObject *Obj, const char *Op) const {
  static SRAGraphObjInfo graph_SRAGraph_get_binop("get_binop");
  return graph_SRAGraph_get_binop({get(), Obj, Get(Op)});
}

PyObject *RAGraphBase::getConstant(PyObject *Obj) const {
  static SRAGraphObjInfo graph_SRAGraph_get_constant("get_const");
  return graph_SRAGraph_get_constant({get(), Obj});
}

PyObject *RAGraphBase::getInf(PyObject *Obj) const {
  static SRAGraphObjInfo graph_SRAGraph_get_inf("get_inf");
  return graph_SRAGraph_get_inf({get(), Obj});
}

PyObject *RAGraphBase::getPhi(PyObject *Obj) const {
  static SRAGraphObjInfo graph_SRAGraph_get_phi("get_phi");
  return graph_SRAGraph_get_phi({get(), Obj});
}

PyObject *RAGraphBase::getSigma(PyObject *Obj, const char *Op) const {
  static SRAGraphObjInfo graph_SRAGraph_get_sigma("get_sigma");
  return graph_SRAGraph_get_sigma({get(), Obj, Get(Op)});
}

