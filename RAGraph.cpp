#define DEBUG_TYPE "ra-graph"

#include "RAGraph.h"

using namespace llvm;
using llvmpy::Get;

RAGraph::RAGraph(Function *F, Redefinition &RDF, SAGENameVault &SNV)
    : RAGraphBase(F, RDF, SNV) {
}

void RAGraph::addValue(Value *V) {
  assert(V->getType()->isIntegerTy() && "Can only add integer values");
  setNode(V, getInf(getNodeName(V)));
}

