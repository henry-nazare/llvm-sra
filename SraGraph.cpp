#define DEBUG_TYPE "sra-graph"

#include "SraGraph.h"

#include "llvm/Support/Debug.h"

using namespace llvm;

struct SraGraphObjInfo : public PythonObjInfo {
  SraGraphObjInfo(const char *Fn)
      : PythonObjInfo("llvmsra.graph", "SraGraph", Fn) {}
};

static SraGraphObjInfo graph_SraGraph(nullptr);
SraGraph::SraGraph() {
  Graph_ = graph_SraGraph({});
}

void SraGraph::addConstantInt(ConstantInt *CI) {
  setNode(CI, getConstant(llvmpy::Get(CI->getValue())));
}

void SraGraph::addNamedConstant(Value *V, std::string Name) {
  setNode(V, getConstant(llvmpy::Get(Name)));
}

void SraGraph::addPhiNode(PHINode *Phi, std::string Name) {
  setNode(Phi, getPhi(llvmpy::Get(Name)));
}

void SraGraph::addSigmaNode(
    PHINode *Sigma, CmpInst::Predicate Pred, std::string Name) {
  setNode(Sigma, getSigma(llvmpy::Get(Name)));
}

void SraGraph::addBinOp(BinaryOperator *BO, std::string Name) {
  switch (BO->getOpcode()) {
    case Instruction::Add:
      setNode(BO, getBinOp(llvmpy::Get(Name), "add"));
      return;
    case Instruction::Sub:
      setNode(BO, getBinOp(llvmpy::Get(Name), "sub"));
      return;
    case Instruction::Mul:
      setNode(BO, getBinOp(llvmpy::Get(Name), "mul"));
      return;
    default:
    case Instruction::SDiv:
    case Instruction::UDiv:
      setNode(BO, getBinOp(llvmpy::Get(Name), "div"));
      return;
  }
  assert(false && "Unhandled binary operator");
}

void SraGraph::addIncoming(Value *V, Value *Incoming) {
  addIncoming(getNode(V), getNode(Incoming));
}

void SraGraph::solve() const {
  static SraGraphObjInfo graph_SraGraph_solve("solve");
  graph_SraGraph_solve({Graph_});
}

SAGERange SraGraph::getRange(Value *V) const {
  return SAGERange(getNode(V));
}

PyObject *SraGraph::getNode(Value *V) const {
  // TODO: we also want to handle other constants, such as UndefValue.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    return getConstant(llvmpy::Get(CI->getValue()));
  }

  auto It = Nodes_.find(V);
  assert(It != Nodes_.end() && "Node not found in map");
  return It->second;
}

void SraGraph::setNode(Value *V, PyObject *Obj) {
  DEBUG(dbgs() << "setNode: " << *V << ", " << *Obj << "\n");
  Nodes_[V] = Obj;
}

PyObject *SraGraph::getConstant(PyObject *Obj) const {
  static SraGraphObjInfo graph_SraGraph_get_constant("get_const");
  return graph_SraGraph_get_constant({Graph_, Obj});
}

PyObject *SraGraph::getPhi(PyObject *Obj) const {
  static SraGraphObjInfo graph_SraGraph_get_phi("get_phi");
  return graph_SraGraph_get_phi({Graph_, Obj});
}

PyObject *SraGraph::getSigma(PyObject *Obj) const {
  static SraGraphObjInfo graph_SraGraph_get_sigma("get_sigma");
  return graph_SraGraph_get_sigma({Graph_, Obj, llvmpy::Get("lt")});
}

PyObject *SraGraph::getBinOp(PyObject *Obj, const char *Op) const {
  static SraGraphObjInfo graph_SraGraph_get_binop("get_binop");
  return graph_SraGraph_get_binop({Graph_, Obj, llvmpy::Get(Op)});
}

void SraGraph::addIncoming(PyObject *Node, PyObject *Incoming) const {
  static SraGraphObjInfo graph_SraGraph_add_incoming("add_edge");
  graph_SraGraph_add_incoming({Graph_, Node, Incoming});
}

