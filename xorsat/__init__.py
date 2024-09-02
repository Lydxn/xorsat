from xorsat._xorsat import *
from xorsat._xorsat import _solve_zeros


class Solver:
    def __init__(self):
        self.constraints = []

    def add(self, *args):
        self.constraints += args

    def solve(self, all=False):
        zeros = []
        for constraint in self.constraints:
            for z in constraint.zeros():
                if z.is_constant():
                    if z.compl == 1:
                        raise ValueError('no solution')
                    else:
                        continue
                zeros.append(z)
        return _solve_zeros(zeros, all)
