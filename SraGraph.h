#ifndef _SRAGRAPH_H_
#define _SRAGRAPH_H_

#include "SAGE/Python/PythonInterface.h"
#include "SAGE/SAGERange.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

#include <map>

using namespace llvm;

class SraGraph {
public:
  SraGraph();

  void addConstantInt(ConstantInt *CI);
  void addNamedConstant(Value *V, std::string Name);
  void addPhiNode(PHINode *Phi, std::string Name);
  void addSigmaNode(PHINode *Sigma, CmpInst::Predicate Pred, std::string Name);
  void addBinOp(BinaryOperator *BO, std::string Name);

  void addIncoming(Value *V, Value *Incoming);

  void solve() const;

  SAGERange getRange(Value *V) const;

protected:
  PyObject *getNode(Value *V) const;
  void setNode(Value *V, PyObject *Obj);

  PyObject *getConstant(PyObject *Obj) const;
  PyObject *getPhi(PyObject *Obj) const;
  PyObject *getSigma(PyObject *Obj) const;
  PyObject *getBinOp(PyObject *Obj, const char *Op) const;

  void addIncoming(PyObject *Node, PyObject *Incoming) const;

private:
  PyObject *Graph_;
  std::map<Value*, PyObject*> Nodes_;
};

#endif

