from xorsat import *
from xorsat.crypto import MT19937
import random

random_bits = [random.getrandbits(1) for _ in range(20000)]
guess = random.getrandbits(128)

L = LinearSystem(**{'mt%d' % i: 32 for i in range(624)})
mt = L.gens()

s = Solver()
rng = MT19937(mt)
for b in random_bits:
    s.add(rng.getrandbits(1) == b)

recovered_mt = list(s.solve().values())
rng = MT19937(recovered_mt)
for _ in range(len(random_bits)):
    rng()

# Check the recovered state is correct
assert rng.getrandbits(128) == guess
