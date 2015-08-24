from llvmsage.expr import Expr
from llvmsage.range import Range
from llvmsra.range import SraRange

from sage.all import oo, DiGraph
from sympy import S

from itertools import chain
from operator import attrgetter

import operator

bottom_expr = Expr("_BOT_")
bottom_range = Range(bottom_expr, bottom_expr)

def debug(desc, *args):
  pass

class Struct:
  def __init__(self, **entries):
    self.__dict__.update(entries)

class Node:
  def __init__(self, name, id=None, state=bottom_range):
    self.name = str(name)
    self.id = id
    self.expr = Expr(name)
    self.changed = Struct(lower = False, upper = False)
    self.asmp = Expr(True)
    self.state = state
    self.incoming = []

  def add_incoming(self, node):
    self.incoming.append(node)

  def get_incoming_states(self):
    return map(attrgetter("state"), self.incoming)

  def get_incoming_exprs(self):
    return map(attrgetter("expr"), self.incoming)

  def get_incoming_asmp(self):
    asmps = map(attrgetter("asmp"), self.incoming)
    return reduce(operator.and_, asmps, Expr(True))

  def set_state(self, state):
    self.changed = Struct(
        lower = self.state.lower != state.lower, \
        upper = self.state.upper != state.upper)
    # TODO: this 8 shouldn't be hardcoded.
    if state.lower.size() > 8 or state.upper.size() > 8:
      self.state = Range(Expr(-oo), Expr(oo))
    else:
      self.state = state

  def eval_it(self):
    state = self.op(*self.get_incoming_states())
    self.set_state(state)
    self.asmp = self.op_asmp(*self.incoming)

  def eval_it_and_widen(self):
    self.set_state(self.op_widen(self.op(*self.get_incoming_states())))
    self.asmp = self.op_asmp(*self.incoming)

  def eval_narrow(self):
    self.set_state(self.op_narrow(*self.get_incoming_states()))
    self.asmp = self.op_asmp_narrow(*self.incoming)

  def op(self, *any):
    debug("op (id):", self)
    return self.state

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

  def op_asmp(self, *any):
    return Expr(True)

  def __repr__(self):
    return str(self.name) + "\n\trange = " + str(self.state) + "\n\tasmp = " \
        + str(self.asmp)

  def __str__(self):
    return str(self.name)

class ConstNode(Node):
  pass

class PhiNode(Node):
  def __init__(self, name, **kwargs):
    Node.__init__(self, name, **kwargs)
    self.has_single_initial_value = None
    self.single_initial_value = None

  def op(self, *incoming):
    if len(incoming) > 8:
      debug("op (meet):", self, len(incoming), "[stop]")
      return Range(Expr(-oo), Expr(oo))

    debug("op (meet):", self, len(incoming), incoming)
    return reduce(lambda acc, i: SraRange.meet(acc, i), \
        [i for i in incoming if i != bottom_range])

  def op_asmp(self, *incoming):
    debug("op_asmp (meet):", self, incoming)
    nodes = [node for node in incoming if node.state != bottom_range]
    asmps = [set(node.asmp.expr.args) for node in nodes]
    assert asmps != []

    # Variables that dominate the phi are evaluated first. If a bound reaches a
    # fixed point, the the dominating value can be either a lower or upper
    # bound.
    if self.has_single_initial_value == None:
      if len(nodes) == 1:
        self.has_single_initial_value = True
        self.single_initial_value = nodes[0]
      else:
        self.has_single_initial_value = False

    isect = reduce(operator.and_, asmps)
    res = reduce(operator.and_, map(Expr, isect), Expr(True))
    if self.state != bottom_range and self.has_single_initial_value:
      if not self.changed.lower:
        res = res & (self.expr >= self.single_initial_value.expr)
      if not self.changed.upper:
        res = res & (self.expr <= self.single_initial_value.expr)

    return res

class SigmaNode(Node):
  def __init__(self, name, predicate_op, **kwargs):
    Node.__init__(self, name, **kwargs)
    self.predicate_op = predicate_op

  def op(self, incoming, *any):
    debug("op (incoming_id):", self, incoming)
    return incoming

  def op_narrow(self, incoming, bound):
    debug("op_narrow:", self, incoming, bound)
    return SraRange.narrow_on(self.predicate_op, incoming, bound)

  def op_asmp(self, incoming, bound):
    debug("op_asmp (join):", self, incoming, bound)
    return (self.expr.eq(incoming.expr)) & incoming.asmp & bound.asmp

  def op_asmp_narrow(self, incoming, bound):
    debug("op_asmp_narrow:", self, incoming, bound)
    return (self.expr.eq(incoming.expr)) & incoming.asmp & bound.asmp \
        & self.predicate_op(self.expr, bound.expr)

class BinopNode(Node):
  def __init__(self, name, binop, **kwargs):
    Node.__init__(self, name, **kwargs)
    self.binop = binop

  def op(self, lhs, rhs):
    debug("op (binop):", self, lhs, rhs)
    return self.binop(lhs, rhs)

  def op_asmp(self, lhs, rhs):
    debug("op_asmp (binop):", self, lhs, rhs)
    return (self.expr.eq(self.binop(lhs.expr, rhs.expr))) & lhs.asmp & rhs.asmp

class SraGraphSolver:
  def __init__(self):
    self.graph = DiGraph()

  def add_edge(self, node_from, node_to):
    self.graph.add_edge(node_from, node_to)
    node_to.add_incoming(node_from)

  def solve(self):
    self.solve_()

  def solve_(self):
    scc_graph = self.graph.strongly_connected_components_digraph()
    for scc_verts in scc_graph.topological_sort():
      self.solve_scc(scc_verts)

  def solve_scc(self, nodes):
    ordered_nodes = sorted(nodes, key=attrgetter("id"))
    debug("solve_scc:", ordered_nodes)

    if len(ordered_nodes) == 1:
      node = ordered_nodes[0]
      node.eval_it()
      if isinstance(node, SigmaNode):
        node.eval_narrow()
      return

    # Iteration phase.
    for _ in xrange(2):
      for node in ordered_nodes:
        node.eval_it()

    # Iteration & widening phase.
    for node in ordered_nodes:
      node.eval_it_and_widen()

    narrow_start_index = next((i for i, node in enumerate(ordered_nodes) \
        if isinstance(node, SigmaNode)), -1)
    if narrow_start_index == -1:
      return

    # Narrowing phase.
    for node in ordered_nodes[narrow_start_index:] \
        + ordered_nodes[:narrow_start_index]:
      if isinstance(node, SigmaNode):
        node.eval_narrow()
      else:
        node.eval_it()

  def __repr__(self):
    return self.__str__()

  def __str__(self):
    ordered_nodes = \
        sorted(self.graph.get_vertices().keys(), key=attrgetter("id"))
    return "\n".join(map(repr, ordered_nodes))

class SraGraph(SraGraphSolver):
  def __init__(self):
    SraGraphSolver.__init__(self)
    self.next_id = 0

  def get_next_id(self):
    next_id = self.next_id
    self.next_id = next_id + 1
    return next_id

  def get_const(self, name):
    return ConstNode(name, state=Range(Expr(name), Expr(name)))

  def get_phi(self, name):
    return PhiNode(name, id=self.get_next_id())

  def get_sigma(self, name, predicate):
    predicate_ops = { "lt": operator.lt, "gt": operator.gt }
    return SigmaNode(name, predicate_ops[predicate], id=self.get_next_id())

  def get_binop(self, name, binop):
    binop_ops = {
      "add": operator.add, "sub": operator.sub, "mul": operator.mul,
      "div": operator.div,
    }
    return BinopNode(name, binop_ops[binop], id=self.get_next_id())

  def get_node(self, name):
    for node in self.graph.get_vertices().keys():
      if node.name == name:
        return node
    return None

