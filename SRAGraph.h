#ifndef _SRAGRAPH_H_
#define _SRAGRAPH_H_

// Should always be the first include.
#include "SAGE/Python/PythonInterface.h"

#include "Redefinition.h"

#include "SAGE/SAGENameVault.h"
#include "SAGE/SAGERange.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

#include <map>
#include <set>

using namespace llvm;

class SRAGraph : public llvmpy::PyObjectHolder {
public:
  SRAGraph(Function *F, Redefinition &RDF, SAGENameVault &SNV);

  SAGERange getRange(Value *V) const;

private:
  void initialize();
  void initializeArguments();
  void initializeIntInsts();
  void initializeIncoming();

  void solve() const;

  void setNode(Value *V, PyObject *Node);
  PyObject *getNode(Value *V);

  PyObject *getNodeName(Value *V) const;

  void addArgument(Argument *A);
  void addIntInst(Instruction *I);

  void addBinOp(BinaryOperator *BO);
  void addPhiNode(PHINode *Phi);
  void addSigmaNode(PHINode *Sigma, CmpInst::Predicate Pred);

  PyObject *getBinOp(PyObject *Obj, const char *Op) const;
  PyObject *getConstant(PyObject *Obj) const;
  PyObject *getPhi(PyObject *Obj) const;
  PyObject *getSigma(PyObject *Sigma, const char *Op) const;

  template <typename T>
  void addIncoming(iterator_range<T> RangeFrom, Value *To) {
    for (auto &From : RangeFrom) {
      addIncoming(From, To);
    }
  }

  void addIncoming(Value *V, Value *Incoming);
  void addIncoming(PyObject *Node, PyObject *Incoming) const;

private:
  Function *F_;
  Redefinition &RDF_;
  SAGENameVault &SNV_;

  std::map<Value*, PyObject*> Node_;
  std::set<Instruction*> NodesWithIncoming_;
};

#endif

