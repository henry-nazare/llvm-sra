from sage.all import DiGraph
from operator import attrgetter

def debug(desc, *args):
  pass

class Node:
  def __init__(self, name, id=None, state=None):
    self.name = str(name)
    self.id = id
    self.state = state
    self.incoming = []

  def add_incoming(self, node):
    self.incoming.append(node)

  def get_incoming_states(self):
    return map(attrgetter("state"), self.incoming)

  def set_state(self, state):
    self.state = state

  def eval_it(self):
    self.set_state(self.op(*self.get_incoming_states()))

  def eval_it_and_widen(self):
    self.set_state(self.op_widen(self.op(*self.get_incoming_states())))

  def eval_narrow(self):
    self.set_state(self.op_narrow(*self.get_incoming_states()))

  def op(self, *any):
    debug("op (id):", self)
    return self.state

  def op_widen(self, state):
    debug("op_widen (id):", self, state)
    return state

  def op_narrow(self, *any):
    debug("op_narrow (id):", self)
    return self.state

  def __repr__(self):
    return "(" + str(self.name) + ", " + str(self.state) + ")"

  def __str__(self):
    return str(self.name)

class NarrowNode(Node):
  pass

class GraphSolver:
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

  def solve_scc(self, nodes, iterations=2):
    ordered_nodes = sorted(nodes, key=attrgetter("id"))
    debug("solve_scc:", ordered_nodes)

    if len(ordered_nodes) == 1:
      node = ordered_nodes[0]
      node.eval_it()
      if isinstance(node, NarrowNode):
        node.eval_narrow()
      return

    # Iteration phase.
    for _ in xrange(iterations):
      for node in ordered_nodes:
        node.eval_it()

    # Iteration & widening phase.
    for node in ordered_nodes:
      node.eval_it_and_widen()

    narrow_start_index = next((i for i, node in enumerate(ordered_nodes) \
        if isinstance(node, NarrowNode)), -1)
    if narrow_start_index == -1:
      return

    # Narrowing phase.
    for node in ordered_nodes[narrow_start_index:] \
        + ordered_nodes[:narrow_start_index]:
      if isinstance(node, NarrowNode):
        node.eval_narrow()
      else:
        node.eval_it()

  def __repr__(self):
    return self.__str__()

  def __str__(self):
    ordered_nodes = \
        sorted(self.graph.get_vertices().keys(), key=attrgetter("id"))
    return "\n".join(map(repr, ordered_nodes))

class Graph(GraphSolver):
  def __init__(self):
    GraphSolver.__init__(self)

  def add_node(self, node):
    node.id = self.graph.order()
    self.graph.add_vertex(node)

  def get_node(self, name):
    for node in self.graph.get_vertices().keys():
      if node.name == name:
        return node
    return None

