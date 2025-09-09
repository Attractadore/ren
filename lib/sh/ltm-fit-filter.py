#!/usr/bin/env python3
import numpy as np
from scipy import fft as fft
from scipy import signal as dsp
from scipy import optimize as opt
from matplotlib import pyplot as plt

# Create an 8-tap downsampling filter to use with a biquadratic upsampling filter.
# Inspired by Bart Wronski's downsampling filter:
# https://bartwronski.com/2020/04/14/bilinear-texture-filtering-artifacts-alternatives-and-frequency-domain-analysis/
# https://bartwronski.com/2021/02/15/bilinear-down-upsampling-pixel-grids-and-that-half-pixel-offset/
# https://bartwronski.com/2022/03/07/fast-gpu-friendly-antialiasing-downsampling-filter/
# https://www.shadertoy.com/view/fsjBWm
# 
# Downsampling filter will look like this (bottom right, +U/+V quadrant):
# [w_4 w_2 w_1 w_0]
# [w_2 w_3 0.0 0.0]
# [w_1 0.0 0.0 0.0]
# [w_0 0.0 0.0 0.0]

np.set_printoptions(linewidth=120)

FN = 512

def lanczos_kernel(x, a):
  return np.sinc(x) * np.sinc(x/a)

def make_lanczos_if(a):
  N = 4 * a
  M = 2 * a
  f = np.zeros(N)
  for i in range(M):
    x = 0.25 + i * 0.5
    f[M + i] = f[M - 1 - i] = lanczos_kernel(x, a)
  return f / np.sum(f)

IF_NEAREST = np.array([1, 1]) / 2
IF_LINEAR = np.array([1, 3, 3, 1]) / 8
IF_QUADRATIC = np.array([1, 9, 22, 22, 9, 1]) / 64
IF_LANCZOS2 = make_lanczos_if(2)
IF_LANCZOS3 = make_lanczos_if(3)
IF_LANCZOS4 = make_lanczos_if(4)
IF_LANCZOS16 = make_lanczos_if(16)

IF = IF_QUADRATIC
IF_FFT = fft.fft2(np.outer(IF, IF), s=[2*FN,2*FN])[:FN,:FN]

def make_lanczos_df(a):
  N = 4 * a
  M = 2 * a
  f = np.zeros(N)
  for i in range(M):
    x = 0.25 + i * 0.5
    f[M + i] = f[M - 1 - i] = lanczos_kernel(x, a)
  return f / np.sum(f)

DF_BOX = np.full((2, 2), 0.25)
DF_LANCZOS3 = make_lanczos_df(3)

def filter_loss(f):
  h = fft.fft2(f, s=[2*FN, 2*FN])[:FN,:FN]
  # h[:FN//2,:FN//2] *= IF_FFT[:FN//2,:FN//2]
  mse = (
    np.sum((np.abs(h[:FN//2,:FN//2]) - 1)**2) +
    np.sum(np.abs(h[:FN//2,FN//2:])**2) +
    np.sum(np.abs(h[FN//2:,:FN//2])**2) +
    np.sum(np.abs(h[FN//2:,FN//2:])**2)
  )
  mse = mse / (FN * FN)
  return mse

"""
def make_filter(w):
  w = np.clip(w, a_min=0, a_max=1)

  wa = -w[0]
  a = w[1]
  w1 = wa * (1 - a)
  w0 = wa * a

  wb = w[2]
  b = w[3]
  w2 = wb * (1 - b) * b
  w3 = wb * b * b
  w4 = wb * (1 - b) * (1 - b)

  f = np.array([
  [ 0,  0,  0, w0, w0,  0,  0,  0],
  [ 0,  0,  0, w1, w1,  0,  0,  0],
  [ 0,  0, w3, w2, w2, w3,  0,  0],
  [w0, w1, w2, w4, w4, w2, w1, w0],
  [w0, w1, w2, w4, w4, w2, w1, w0],
  [ 0,  0, w3, w2, w2, w3,  0,  0],
  [ 0,  0,  0, w1, w1,  0,  0,  0],
  [ 0,  0,  0, w0, w0,  0,  0,  0]])
  f /= f.sum()

  return f
"""

def make_filter(w):
  w = np.concat((w, np.flip(w)))
  f = np.outer(w, w)
  return f / f.sum()

def make_tap_list(w):
  w = np.clip(w, a_min=0, a_max=1)
  w = [float(e) for e in w]
  wa = -w[0]
  a = w[1]
  wb = w[2]
  b = w[3]
  s = 4 * wa + 4 * wb
  wa = wa / s
  wb = wb / s
  taps = []
  for x, y in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
    taps.append((wa, (2.5 + a) * x, (2.5 + a) * y))
  for x, y in [(-1, -1), (1, -1), (-1, 1), (1, 1)]:
    taps.append((wb, (0.5 + b) * x, (0.5 + b) * y))
  taps.sort()
  return taps

def loss(w):
  return filter_loss(make_filter(w))

w = np.array([-0.5, -1, 2, 6])
print(w, loss(w))
print(make_filter(w))
w = opt.minimize(loss, w).x
print(w, loss(w))
print(make_filter(w))
w = np.concat((w, np.flip(w)))
w /= np.sum(w)
print(w, np.sum(w))

# for tap in make_tap_list(w):
#   print(tap)
