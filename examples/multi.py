from xorsat import *

L = LinearSystem(a=1, b=1, c=1, d=1)
a, b, c, d = L.gens()

s = Solver()
s.add(a ^ b ^ c == 1)
s.add(b ^ d == 0)
s.add(a ^ c == 1)

for sol in s.solve(all=True):
    print(sol)

# {'a': 1, 'b': 0, 'c': 0, 'd': 0}
# {'a': 0, 'b': 0, 'c': 1, 'd': 0}