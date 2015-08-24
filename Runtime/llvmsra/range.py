from llvmsage.expr import Expr, S
from llvmsage.range import Range

import operator

class SraRange:
  @staticmethod
  def meet(first, second):
    return \
        Range(first.lower.min(second.lower), first.upper.max(second.upper))

  @staticmethod
  def narrow_on(op, lhs, rhs):
    if op == operator.lt:
      return Range(lhs.lower, lhs.upper.min(rhs.upper - S.One))
    elif op == operator.le:
      return Range(lhs.lower, lhs.upper.min(rhs.upper))
    elif op == operator.gt:
      return Range(lhs.lower.max(rhs.lower + S.One), lhs.upper)
    elif op == operator.ge:
      return Range(lhs.lower.max(rhs.lower), lhs.upper)
    assert False

