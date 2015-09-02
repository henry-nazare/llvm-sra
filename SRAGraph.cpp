#define DEBUG_TYPE "sra-graph"

#include "SRAGraph.h"

using namespace llvm;
using llvmpy::Get;

SRAGraph::SRAGraph(Function *F, Redefinition &RDF, SAGENameVault &SNV)
    : RAGraphBase(F, RDF, SNV) {
}

void SRAGraph::addValue(Value *V) {
  assert(V->getType()->isIntegerTy() && "Can only add integer values");
  setNode(V, getConstant(getNodeName(V)));
}

