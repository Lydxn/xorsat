# xorsat - A fast XOR-SAT solver

`xorsat` is a Python package written in C that efficiently solves mathematical equations
reducible to XOR-SAT.

This library was originally created to crack linear ciphers/PRNGs like the
[Mersenne Twister](https://en.wikipedia.org/wiki/Mersenne_Twister). This library is
suitable as an alternative to `z3` for solving large linear systems, which a typical
SMT solver might struggle at.

Note that this cannot be used to solve general mathematical equations. This includes
equations over GF(2) that contain nonlinearity. If that is what you are looking for,
consider using an SAT/SMT solver or other algebraic cryptanalysis techniques.

## Installation

There is no PyPI package yet, but you can install the package locally by cloning the
repository and running

```
pip install .
```

The project is still in development, so it has been tested on Python 3.10.12 but may fail
with older/newer Python versions.

## Usage

In `xorsat`, all variables are generated through the `LinearSystem` class. The
constructor accepts `key=value` pairs that specify the variable's name and how many bits
we want it to hold. It is roughly equivalent to `z3`'s `key = BitVec(value)`.

Solving constraints closely matches `z3`'s API, except that we use `solve()` instead of
`check() + model()`. Due to algorithmic limitations, inequailities like `<` or `!=` are
not supported.

### Shift-register generator recovery

The following demonstrates how to use `xorsat` to reverse the state of a linear generator:

```py
from xorsat import *
import secrets

x0 = x1 = secrets.randbits(64)
x1 ^= (x1 << 13) % 2**64
x1 ^= x1 >> 7
x1 ^= (x1 << 17) % 2**64

L = LinearSystem(x=64)
x, = L.gens()

x ^= x << 13
x ^= LShR(x, 7)
x ^= x << 17

s = Solver()
s.add(x == x1)
assert s.solve()['x'] == x0
```

### Finding all solutions

You can also pass `all=True` to `solve()` to search for all possible solutions. The
function will return a generator that iterates over the entire solution space.

```py
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
```

### Mersenne Twister recovery

Cracking CPython's `random` is also simple. An implementation of `MT19937` is provided
under the `xorsat.crypto` module. Recovering the entire MT state should take under 10
seconds on average.

```py
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
```