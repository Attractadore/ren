#!/usr/bin/env python3
import numpy as np
from matplotlib import pyplot as plt

def f(x):
  x2 = x * x
  x3 = x2 * x
  x4 = x2 * x2
  x5 = x4 * x
  x6 = x4 * x2
  x7 = x6 * x
  return -17.86 * x7 + 78.01 * x6 - 126.7 * x5 + 92.06 * x4 - 28.72 * x3 + 4.361 * x2 - 0.1718 * x + 0.002857;

x = np.linspace(0, 1, num=1000)
y = f(x)

plt.plot(x, y, label="AgX default contrast curve")

deg = 9
a = np.polyfit(y, x, deg)
p = np.polyval(a, y)
err = np.sqrt(np.mean((p - x) ** 2))
plt.plot(p, y, label=f"Degree {deg} inverse: {err}")

plt.legend()
plt.show()

np.set_printoptions(suppress=True)
print(a)
