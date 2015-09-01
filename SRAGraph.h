#ifndef _SRAGRAPH_H_
#define _SRAGRAPH_H_

// Should always be the first include.
#include "SAGE/Python/PythonInterface.h"

#include "Redefinition.h"

#include "SAGE/SAGEAnalysisGraph.h"
#include "SAGE/SAGENameVault.h"
#include "SAGE/SAGERange.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

#include <set>

using namespace llvm;

class SRAGraph : public SAGEAnalysisGraph {
public:
  SRAGraph(Function *F, Redefinition &RDF, SAGENameVault &SNV);

  SAGERange getRange(Value *V) const;

private:
  void initialize();
  void initializeArguments();
  void initializeIntInsts();
  void initializeIncoming();

  virtual PyObject *getNode(Value *V);
  virtual PyObject *getNodeName(Value *V) const;

  void addArgument(Argument *A);
  void addIntInst(Instruction *I);

  void addBinOp(BinaryOperator *BO);
  void addPhiNode(PHINode *Phi);
  void addSigmaNode(PHINode *Sigma, CmpInst::Predicate Pred);

  PyObject *getBinOp(PyObject *Obj, const char *Op) const;
  PyObject *getConstant(PyObject *Obj) const;
  PyObject *getPhi(PyObject *Obj) const;
  PyObject *getSigma(PyObject *Sigma, const char *Op) const;

private:
  Function *F_;
  Redefinition &RDF_;

  std::set<Instruction*> NodesWithIncoming_;
};

#endif

