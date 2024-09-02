from xorsat import *
import secrets

# x1 = xorshift64(x0)
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