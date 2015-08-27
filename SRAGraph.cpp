#define DEBUG_TYPE "sra-graph"

#include "SRAGraph.h"

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

static bool IsSigmaNode(Instruction *I, Redefinition &RDF) {
  PHINode *Phi = dyn_cast<PHINode>(I);
  return Phi && Phi->getNumIncomingValues() == 1 &&
      RDF.getRedef(Phi->getIncomingValue(0), Phi->getParent()) == Phi;
}

static CmpInst::Predicate GetSwappedInversePredicate(ICmpInst *ICI) {
  ICmpInst::Predicate Pred = ICI->getPredicate();
  if (ICI->isEquality()) {
    return Pred;
  } else if (ICI->isTrueWhenEqual()) {
    return (ICmpInst::Predicate)(Pred - 1);
  }

  assert(ICI->isFalseWhenEqual());
  return (ICmpInst::Predicate)(Pred + 1);
}

static std::pair<CmpInst::Predicate, Value*>
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
      : std::make_pair(GetSwappedInversePredicate(ICI), LHS);
}

static SRAGraphObjInfo graph_SRAGraph(nullptr);
SRAGraph::SRAGraph(Function *F, Redefinition &RDF, SraNameVault &SNV)
    : PyObjectHolder(graph_SRAGraph({})), F_(F), RDF_(RDF), SNV_(SNV) {
  initialize();
  solve();
}

SAGERange SRAGraph::getRange(Value *V) const {
  static PythonAttrInfo graph_Node_state("state");
  auto It = Node_.find(V);
  assert(It != Node_.end() && "Requested value not in map");
  return graph_Node_state.get(It->second);
}

void SRAGraph::initialize() {
  initializeArguments();
  initializeIntInsts();
  initializeIncoming();
}

void SRAGraph::initializeArguments() {
  for (auto &A : F_->args()) {
    if (A.getType()->isIntegerTy()) {
      addArgument(&A);
    }
  }
}

void SRAGraph::initializeIntInsts() {
  ReversePostOrderTraversal<Function*> RPOT(F_);
  for (auto &BB : Range(RPOT.begin(), RPOT.end())) {
    for (auto &I : *BB) {
      if (I.getType()->isIntegerTy()) {
        addIntInst(&I);
      }
    }
  }
}

void SRAGraph::initializeIncoming() {
  for (auto &I : NodesWithIncoming_) {
    addIncoming(I->operands(), I);
    if (IsSigmaNode(I, RDF_)) {
      auto Bound = GetSigmaBound(cast<PHINode>(I), RDF_);
      addIncoming(Bound.second, I);
    }
  }
}

void SRAGraph::solve() const {
  static SRAGraphObjInfo graph_SRAGraph_solve("solve");
  graph_SRAGraph_solve({get()});
}

void SRAGraph::setNode(Value *V, PyObject *Node) {
  Node_[V] = Node;
}

PyObject *SRAGraph::getNode(Value *V) {
  assert(V->getType()->isIntegerTy() && "Value is not an integer");
  auto It = Node_.find(V);

  // TODO: we also want to handle other constants, such as UndefValue.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    PyObject *Node = getConstant(Get(CI->getValue()));
    It = Node_.insert(std::make_pair(CI, Node)).first;
  } else if (isa<Constant>(V)) {
    PyObject *Node = getConstant(getNodeName(V));
    It = Node_.insert(std::make_pair(CI, Node)).first;
  }

  assert(It != Node_.end() && "Requested value not in node map");
  return It->second;
}

PyObject *SRAGraph::getNodeName(Value *V) const {
  // TODO: we also want to handle other constants, such as UndefValue.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    return Get(CI->getValue());
  }

  return Get(SNV_.getName(V));
}

void SRAGraph::addArgument(Argument *A) {
  assert(A->getType()->isIntegerTy() && "Can only add integer arguments");
  setNode(A, getConstant(getNodeName(A)));
}

void SRAGraph::addIntInst(Instruction *I) {
  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::SDiv:
    case Instruction::UDiv:
      addBinOp(cast<BinaryOperator>(I));
      break;
    case Instruction::PHI: {
      PHINode *Phi = cast<PHINode>(I);
      if (IsSigmaNode(Phi, RDF_)) {
        auto Bound = GetSigmaBound(Phi, RDF_);
        addSigmaNode(Phi, Bound.first);
        break;
      }
      addPhiNode(Phi);
      break;
    }
    default:
      setNode(I, getConstant(getNodeName(I)));
      break;
  }
}

void SRAGraph::addBinOp(BinaryOperator *BO) {
  NodesWithIncoming_.insert(BO);
  switch (BO->getOpcode()) {
    case Instruction::Add:
      setNode(BO, getBinOp(getNodeName(BO), "add"));
      return;
    case Instruction::Sub:
      setNode(BO, getBinOp(getNodeName(BO), "sub"));
      return;
    case Instruction::Mul:
      setNode(BO, getBinOp(getNodeName(BO), "mul"));
      return;
    case Instruction::SDiv:
    case Instruction::UDiv:
      setNode(BO, getBinOp(getNodeName(BO), "div"));
      return;
    default:
      break;
  }
  assert(false && "Unhandled binary operator");
}

void SRAGraph::addPhiNode(PHINode *Phi) {
  NodesWithIncoming_.insert(Phi);
  setNode(Phi, getPhi(getNodeName(Phi)));
}

void SRAGraph::addSigmaNode(PHINode *Sigma, CmpInst::Predicate Pred) {
  NodesWithIncoming_.insert(Sigma);
  switch (Pred) {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_ULT:
      setNode(Sigma, getSigma(getNodeName(Sigma), "lt"));
      return;
    case ICmpInst::ICMP_SLE:
    case ICmpInst::ICMP_ULE:
      setNode(Sigma, getSigma(getNodeName(Sigma), "le"));
      return;
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_UGT:
      setNode(Sigma, getSigma(getNodeName(Sigma), "gt"));
      return;
    case ICmpInst::ICMP_SGE:
    case ICmpInst::ICMP_UGE:
      setNode(Sigma, getSigma(getNodeName(Sigma), "ge"));
      return;
    default:
      break;
  }
  assert(false && "Unknown predicate");
}

void SRAGraph::addIncoming(Value *From, Value *To) {
  addIncoming(getNode(From), getNode(To));
}

PyObject *SRAGraph::getBinOp(PyObject *Obj, const char *Op) const {
  static SRAGraphObjInfo graph_SRAGraph_get_binop("get_binop");
  return graph_SRAGraph_get_binop({get(), Obj, Get(Op)});
}

PyObject *SRAGraph::getConstant(PyObject *Obj) const {
  static SRAGraphObjInfo graph_SRAGraph_get_constant("get_const");
  return graph_SRAGraph_get_constant({get(), Obj});
}

PyObject *SRAGraph::getPhi(PyObject *Obj) const {
  static SRAGraphObjInfo graph_SRAGraph_get_phi("get_phi");
  return graph_SRAGraph_get_phi({get(), Obj});
}

PyObject *SRAGraph::getSigma(PyObject *Obj, const char *Op) const {
  static SRAGraphObjInfo graph_SRAGraph_get_sigma("get_sigma");
  return graph_SRAGraph_get_sigma({get(), Obj, Get(Op)});
}

void SRAGraph::addIncoming(PyObject *From, PyObject *To) const {
  static SRAGraphObjInfo graph_SRAGraph_add_incoming("add_edge");
  graph_SRAGraph_add_incoming({get(), From, To});
}

