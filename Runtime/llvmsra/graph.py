from llvmanalysis.graph import Graph, Node, NarrowNode
from llvmsage.expr import Expr, S as SE
from llvmsage.range import Range
from llvmsra.range import SraRange

from sage.all import oo
from sympy import S

import operator

bottom_expr = Expr("_BOT_")
bottom_range = Range(bottom_expr, bottom_expr)

def debug(desc, *args):
  pass

class Struct:
  def __init__(self, **entries):
    self.__dict__.update(entries)

class SraNode(Node):
  def __init__(self, name, state=bottom_range):
    Node.__init__(self, name, state=state)
    self.changed = Struct(lower = False, upper = False)

  def set_state(self, state):
    self.changed = Struct(
        lower = self.state.lower != state.lower, \
        upper = self.state.upper != state.upper)
    if state.lower.size() > 8 or state.upper.size() > 8:
      Node.set_state(self, Range(Expr(-oo), Expr(oo)))
    else:
      Node.set_state(self, state)

  def op_widen(self, state):
    debug("op_widen", self, state)
    new_state = state
    if self.state == bottom_range:
      return new_state
    if bool(new_state.lower.expr == -oo):
      pass
    elif bool(new_state.lower.expr == -oo):
      new_state.lower = Expr(-oo)
    else:
      if new_state.lower != self.state.lower:
        new_state.lower = Expr(-oo)
    if bool(new_state.upper.expr == oo):
      pass
    elif bool(new_state.upper.expr == oo):
      new_state.upper = Expr(oo)
    else:
      if new_state.upper != self.state.upper:
        new_state.upper = Expr(oo)
    return new_state

class ConstNode(SraNode):
  pass

class PhiNode(SraNode):
  def __init__(self, name, **kwargs):
    SraNode.__init__(self, name, **kwargs)
    self.has_single_initial_value = None
    self.single_initial_value = None

  def op(self, *incoming):
    if len(incoming) > 8:
      debug("op (meet):", self, len(incoming), "[stop]")
      return Range(Expr(-oo), Expr(oo))

    debug("op (meet):", self, len(incoming), incoming)
    return reduce(lambda acc, i: SraRange.meet(acc, i), \
        [i for i in incoming if i != bottom_range])

class SigmaNode(SraNode, NarrowNode):
  def __init__(self, name, predicate_op, **kwargs):
    SraNode.__init__(self, name, **kwargs)
    self.predicate_op = predicate_op

  def op(self, incoming, *any):
    debug("op (incoming_id):", self, incoming)
    return incoming

  def op_narrow(self, incoming, bound):
    debug("op_narrow:", self, incoming, bound)
    return SraRange.narrow_on(self.predicate_op, incoming, bound)

class BinopNode(SraNode):
  def __init__(self, name, binop, **kwargs):
    SraNode.__init__(self, name, **kwargs)
    self.binop = binop

  def op(self, lhs, rhs):
    debug("op (binop):", self, lhs, rhs)
    return self.binop(lhs, rhs)

class SRAGraph(Graph):
  def __init__(self):
    Graph.__init__(self)

  def get_const(self, name):
    node = ConstNode(name, state=Range(Expr(name), Expr(name)))
    self.add_node(node)
    return node

  def get_inf(self, name):
    node = ConstNode(name, state=Range(Expr(-oo), Expr(oo)))
    self.add_node(node)
    return node

  def get_phi(self, name):
    node = PhiNode(name)
    self.add_node(node)
    return node

  def get_sigma(self, name, predicate):
    predicate_ops = {
        "lt": operator.lt, "le": operator.le, "gt": operator.gt,
        "ge": operator.ge
    }
    node = SigmaNode(name, predicate_ops[predicate])
    self.add_node(node)
    return node

  def get_binop(self, name, binop):
    binop_ops = {
      "add": operator.add, "sub": operator.sub, "mul": operator.mul,
      "div": operator.div,
    }
    node = BinopNode(name, binop_ops[binop])
    self.add_node(node)
    return node

