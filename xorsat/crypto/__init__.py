from xorsat import *

def _lshr(x, k):
    if isinstance(x, BitVec):
        return LShR(x, k)
    return x >> k


class MersenneTwister:
    def __init__(self, mt, w, n, m, r, a, u, d, s, b, t, c, l):
        w1 = (1 << w) - 1
        if len(mt) != n or min(r, u, s, t, l) > w and max(a, b, c, d) > w1:
            raise ValueError('invalid parameters')

        self.mt = list(mt)
        self.w = w
        self.n = n
        self.m = m
        self.r = r
        self.a = a
        self.u = u
        self.d = d
        self.s = s
        self.b = b
        self.t = t
        self.c = c
        self.l = l

        self.w1 = w1
        self.lmsk = w1 & ((1 << r) - 1)
        self.umsk = w1 ^ self.lmsk
        self.mti = 0

    def twist(self):
        i = self.mti
        y = (self.mt[i] & self.umsk) ^ (self.mt[(i + 1) % self.n] & self.lmsk)
        sel = Broadcast(y, 0) & self.a if isinstance(y, BitVec) else (y & 1) * self.a
        self.mt[i] = self.mt[(i + self.m) % self.n] ^ _lshr(y, 1) ^ sel

    def temper(self, y):
        y ^= _lshr(y, self.u) & self.d
        y ^= (y << self.s) & self.w1 & self.b
        y ^= (y << self.t) & self.w1 & self.c
        y ^= _lshr(y, self.l)
        return y

    def __call__(self):
        self.twist()
        y = self.mt[self.mti]
        self.mti = (self.mti + 1) % self.n
        return self.temper(y)

    def getrandbits(self, k=None):
        """Uses the CPython's implementation of random.getrandbits()"""
        if k is None:
            k = self.w
        if k == 0:
            return 0
        if k <= self.w:
            return _lshr(self.__call__(), self.w - k)
        x = 0
        for i in range(0, k, self.w):
            r = self.__call__()
            if i + self.w > k:
                r = _lshr(r, self.w - (k - i))
            x |= r << i
        return x


class MT19937(MersenneTwister):
    """32-bit Mersenne Twister by Matsumoto and Nishimura, 1998"""
    def __init__(self, mt):
        super().__init__(
            mt, 32, 624, 397, 31,
            0x9908b0df, 11,
            0xffffffff, 7,
            0x9d2c5680, 15,
            0xefc60000, 18,
        )


class MT1993764(MersenneTwister):
    """64-bit Mersenne Twister by Matsumoto and Nishimura, 2000"""
    def __init__(self, mt):
        super().__init__(
            mt, 64, 312, 156, 31,
            0xb5026f5aa96619e9, 29,
            0x5555555555555555, 17,
            0x71d67fffeda60000, 37,
            0xfff7eee000000000, 43,
        )
