#!/usr/bin/env python3

import re
import subprocess

min_level = 1
max_level = 15
time = 250
time_unit = "ms"
min_reps = 4
starting_iter = 0
corpus = "osdb"
size = 2 ** 12

use_numa = False
use_nice = True
use_rr = True
sequential = False

dict_fn = "bench/dicts/%s.zstd-dict" % (corpus,)
in_fn = "bench/tmp/%s-in-%d" % (corpus, size)

pre_args = []

if use_rr or use_nice:
  pre_args += ["sudo"]

if use_rr:
  pre_args += ["chrt", "--rr", "99"]
elif use_nice:
  pre_args += ["nice", "-n", "-10"]

args = [
  "-D", dict_fn,
  "-i", in_fn,
  "-b", str(min_level),
  "-e", str(max_level),
  "-t", str(time) + time_unit,
  "-n", str(min_reps),
  "-s", str(starting_iter),
]

RUN_RE = re.compile(
    r'^(?P<run_name>[A-Za-z0-9_\-.]+) *: *' +
    r'(?P<function>[A-Za-z0-9_]+) *' +
    r'@ lvl *(?P<clevel>[\-0-9]+): *' +
    r'(?P<bytes_in>[0-9]+) *B *-> *' +
    r'(?P<bytes_out>[0-9.]+) *B, *' +
    r'(?P<iters>[0-9]+) *iters, *' +
    r'(?P<total_time>[0-9]+) *ns, *' +
    r'(?P<iter_time>[0-9]+) *ns/iter, *' +
    r'(?P<speed>[0-9.]+) *MB/s$')

def format_float(n, lp=2, tp=3):
  s = "%*.*f" % (lp + tp + 2, tp, n)
  if n == 0:
    return s
  color = "\033[32m" if n > 0 else "\033[31m"
  for i, c in enumerate(s):
    if c not in " -0.":
      return s[:i] + color + s[i:] + "\033[0m"
  return s

def main():
  dev_args = pre_args + (["numactl", "-C", "1", "-m", "1"] if use_numa else []) + ["./framebench-zstd-dev", "-l", "dev"] + args
  exp_args = pre_args + (["numactl", "-C", "0", "-m", "0"] if use_numa else []) + ["./framebench-zstd-exp", "-l", "exp"] + args

  print(" ".join(dev_args))
  print(" ".join(exp_args))

  dev_p = subprocess.Popen(dev_args, stderr=subprocess.PIPE)
  if sequential:
    dev_p.wait()
  exp_p = subprocess.Popen(exp_args, stderr=subprocess.PIPE)

  prev_func = None

  # for dl, el in zip(dev_p.stderr.readlines(), exp_p.stderr.readlines()):
  while True:
    dl = dev_p.stderr.readline()
    el = exp_p.stderr.readline()
    if dl == b"" or el == b"":
      break

    dl = dl.decode("utf-8").rstrip()
    el = el.decode("utf-8").rstrip()
    dm = RUN_RE.match(dl)
    em = RUN_RE.match(el)
    # print(dl)
    # print(el)

    assert dm.group("function") == em.group("function")
    assert dm.group("clevel") == em.group("clevel")
    assert dm.group("bytes_in") == em.group("bytes_in")

    if dm.group("function") != prev_func:
      if prev_func is not None:
        print()
      prev_func = dm.group("function")

    print("%s vs %s: %-30s @ lvl %3s: %8s B -> %11s vs %11s B (%s%%), %7s vs %7s iters, %7s vs %7s MB/s (%s%%)" % (
      dm.group("run_name"),
      em.group("run_name"),
      dm.group("function"),
      dm.group("clevel"),
      dm.group("bytes_in"),
      dm.group("bytes_out"),
      em.group("bytes_out"),
      format_float(100 * (1.0 - float(em.group("bytes_out")) / float(dm.group("bytes_out")))),
      dm.group("iters"),
      em.group("iters"),
      dm.group("speed"),
      em.group("speed"),
      format_float(100 * (float(em.group("speed")) / float(dm.group("speed")) - 1.0))
    ))
    # print("%32s %3s %9s %7s %7s %9s" % (
    #   dm.group("function"),
    #   dm.group("clevel"),
    #   dm.group("bytes_in"),
    #   dm.group("speed"),
    #   em.group("speed"),
    #   format_float(100 * (float(em.group("speed")) / float(dm.group("speed")) - 1.0)) + "%"
    # ))

  dev_p.wait()
  exp_p.wait()

if __name__ == '__main__':
  main()
