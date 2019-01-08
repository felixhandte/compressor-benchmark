#!/usr/bin/env python3

import os
import re
import sys
import glob
import json
import math
import stat
import numpy
import subprocess

sys.path.append(".")

import graph

# (branch, compiler, corpus, function, clevel, size) -> [speeds]

# such as produced by, for example,
# git rev-list --abbrev-commit dev..ref-dict-table^ | tr '\n' ' '
# BRANCH_STRS = "050462e 7f6645c 13e60c7 d203a72 01be2e3 bac7f3d b8f143e 1ac0b6c af3992d 7afa9ed e56b501"
# BRANCH_STRS = "0cecaf5 553b7ac 4d94029"
# BRANCH_STRS = "8aa4578 50d3ed6 52ac9f2 0cecaf5 553b7ac 4d94029"
# BRANCH_STRS = "8aa4578 50d3ed6 4d94029"
# BRANCH_STRS = "5406c2e c32e031 dfed9fa"
# BRANCH_STRS = "3bc57db 1f86f20 dbf373a b095bff"
# BRANCH_STRS = "46108a0 6c57dc6 f622cd9 69aea6b 3bc57db cfdf31b b095bff"
# BRANCH_STRS = "c3b5889 4a2b6a7 8450542 37f220a 9938b17"
# BRANCH_STRS = "da8a12a beca9c7 08c5be5"
# BRANCH_STRS = "a7c75740 43606f9c 5b292b56 d2eb4b9a 9e436879 6b535158"
# BRANCH_STRS = "0c222c15 c986dbf"
BRANCH_STRS = "d70c4a5 27dc078a"
BRANCHES = list(reversed(BRANCH_STRS.split(" ")))
# BRANCHES = list(BRANCH_STRS.split(" "))

BRANCH_PAIRS = [
  # ("e56b501", "1ac0b6c"),
  # ("1ac0b6c", "b8f143e"),
  # ("1ac0b6c", "bac7f3d"),
  # ("1ac0b6c", "01be2e3"),
  # ("1ac0b6c", "d203a72"),
  # ("1ac0b6c", "13e60c7"),
  # ("1ac0b6c", "7f6645c"),
  # ("1ac0b6c", "050462e"),
  # ("e56b501", "050462e"),
  # ("f1b50f3", "6709827"),
  # ("6709827", "2392937"),
  # ("2392937", "4165b7a"),
  # ("4165b7a", "5bfd1a8"),
  # ("5bfd1a8", "e2acdb6"),
  # # ("e2acdb6", "f42aeb3"),
  # ("e2acdb6", "03286d1"),
  # ("4165b7a", "af8fb7c"),
  # ("4165b7a", "5cd227f"),
  # ("4165b7a", "a72f491"),
  # ("4165b7a", "03976f5"),
  # ("f1b50f3", "522b0b1"),
  # ("522b0b1", "256573d"),
  # ("256573d", "22ba1a9"),
  # ("22ba1a9", "79fd954"),
  # ("79fd954", "69c43c5"),
  # ("8c35839", "69c43c5"),
]

for b1, b2 in BRANCH_PAIRS:
  if b1 not in BRANCHES:
    BRANCHES.append(b1)
  if b2 not in BRANCHES:
    BRANCHES.append(b2)

BRANCHES = tuple(BRANCHES)

# print(" ".join(reversed(BRANCHES)))

BR_LABELS = {
  "d70c4a5": "v1.3.5",
  "27dc078a": "v1.3.4",
}

# TEST_DIR = "/home/felixh/dev/home/felixh/local/lz4/tests"
TEST_DIR = "/home/felixh/prog/lz4/tests"
BENCH_DIR = "/home/felixh/prog/compressor-benchmark"
# DATA_DIR = os.path.join(BENCH_DIR, "bench/data")
DATA_DIR = os.path.join(BENCH_DIR, "bench/dev-data")
GEN_DIR = os.path.join(BENCH_DIR, "bench/gen")


WEIGHTS_FILE = "managed-compression-b64-lengths-and-weights"

DEVNULL = open("/dev/null", "wb")

INPUT_SIZE_RE = re.compile(r'^input size: ([0-9]*)$')
RUN_RE = re.compile(
    r'^(?P<run_name>[A-Za-z0-9_\-.]+) *: *' +
    r'(?P<function>[A-Za-z0-9_]+) *' +
    r'@ lvl *(?P<clevel>[\-0-9]+), *' +
    r'(?P<contexts>[\-0-9]+) *ctxs: *' +
    r'(?P<bytes_in>[0-9]+) *B *-> *' +
    r'(?P<bytes_out>[0-9.]+) *B, *' +
    r'(?P<iters>[0-9]+) *iters, *' +
    r'(?P<total_time>[0-9]+) *ns, *' +
    r'(?P<iter_time>[0-9]+) *ns/iter, *' +
    r'(?P<speed>[0-9.]+) *MB/s$')

MIN_INPUT_SIZE = 1

Y_SCALE = 2
Y_INC = 2
Y_ZERO_LOG = 0

Y_MIN_LOG = 0
Y_MAX_LOG = 8

Y_MIN = Y_INC ** Y_MIN_LOG
Y_MAX = Y_INC ** Y_MAX_LOG

X_INC = 2
X_ZERO_LOG = 6

# pos
X_MIN_LOG = 6
X_MAX_LOG = 23

X_MIN = X_INC ** X_MIN_LOG
X_MAX = X_INC ** X_MAX_LOG

# VAL_REDUCER = max
# VAL_REDUCER = lambda x: max(x[-5:]) # max of last 5 runs
# VAL_REDUCER = lambda x: sum(x) / len(x)
# VAL_REDUCER = lambda x: sorted(x)[3 * len(x) // 4] # 75th percentile
VAL_REDUCER = lambda x: sorted(x)[9 * len(x) // 10] # 90th percentile

# def yv2p(v):
#   return (v / Y_INC + Y_ZERO)

# def yp2v(p):
#   return (p - Y_ZERO) * Y_INC

class Axis(object):
  def __init__(self, log=False):
    self._log = log
    self._min_val = None
    self._max_val = None

  def fit(self, vals):
    if vals:
      min_val = min(vals)
      max_val = max(vals)
      if self._min_val is None:
        self._min_val = min_val
      else:
        self._min_val = min(self._min_val, min_val)
      if self._max_val is None:
        self._max_val = max_val
      else:
        self._max_val = min(self._max_val, max_val)

  def min_v(self):
    return self._min_val

  def min_p(self):
    return v2p(self._min_val)

  def max_v(self):
    return self._max_val

  def max_p(self):
    return v2p(self._max_val)

  def v2p(self, v):
    """val to pos"""

  def p2v(self, p):
    """pos to val"""


class Plot(object):
  def __init__(self):
    pass

def log_yv2p(v):
  return (math.log(v, Y_INC) - Y_ZERO_LOG) * Y_SCALE

def log_yp2v(p):
  return (Y_INC ** (p / Y_SCALE + Y_ZERO_LOG))

def log_xv2p(v):
  return math.log(v, X_INC) - X_ZERO_LOG

def log_xp2v(p):
  return (X_INC ** (p + X_ZERO_LOG))


def lin_yv2p(v):
  return (v / Y_LIN_INC + Y_LIN_ZERO)

def lin_yp2v(p):
  return (p - Y_LIN_ZERO) * Y_LIN_INC

def lin_xv2p(v):
  return (v / X_LIN_INC + X_LIN_ZERO)

def lin_xp2v(p):
  return (p - X_LIN_ZERO) * X_LIN_INC

POWERS_OF_1024 = ["", "K", "M", "G", "T", "P"]

X_AXIS_LABEL_FREQUENCY = 2

X_AXIS_LABELS = dict((2 ** (10 * p10 + p), str(2 ** p) + suffix) for p in range(0, 10, X_AXIS_LABEL_FREQUENCY) for p10, suffix in enumerate(POWERS_OF_1024))
X_AXIS_LABELS.update(dict((2 ** p, "$\\frac{1}{" + str(2 ** -p) + "}$") for p in range(-12, 0, X_AXIS_LABEL_FREQUENCY)))

Y_AXIS_LABELS = X_AXIS_LABELS


def print_stats(branches, branch_pairs, start_branch, end_branch, compilers, corpuses, fs, clevels, data):
  for cl in clevels:
    for cc in sorted(compilers):
      for c in sorted(corpuses):
        for f in fs:
        # for f in ("LZ4_compress_default", "LZ4_compress_fast_extState"):
          print("%7s %9s %32s @ %3s @ %8s: %s" % (
            "corpus",
            "cc",
            "function",
            "lvl",
            "size",
            " | ".join("%30s" % (b2[:30],) for b1, b2 in branch_pairs)
          ))
          sizes = set()
          for b in branches:
            sizes |= set(data.get(b, {})
                             .get(cc, {})
                             .get(c, {})
                             .get(f, {})
                             .get(cl, {})
                             .keys())
          for s in sorted(sizes):
            brspeeds = []
            for b1, b2 in branch_pairs:
              ctl = (data.get(b1, {})
                         .get(cc, {})
                         .get(c, {})
                         .get(f, {})
                         .get(cl, {})
                         .get(s, None))
              exp = (data.get(b2, {})
                         .get(cc, {})
                         .get(c, {})
                         .get(f, {})
                         .get(cl, {})
                         .get(s, None))
              if exp:
                d = None
                if ctl:
                  d = (VAL_REDUCER(exp) / VAL_REDUCER(ctl) - 1) * 100
                brspeeds.append((
                  VAL_REDUCER(exp),
                  d,
                  numpy.std(exp) / VAL_REDUCER(exp) * 100
                ))
              else:
                brspeeds.append((None, None, None))
            print("%7s %9s %32s @ %3d @ %8d: %s" % (
              c, cc, f, cl, s,
              " | ".join("%sMB/s %s%%σ %s%%δ" % (
                "%7.1f" % (r,) if r is not None else "    o.o",
                "%5.2f" % (dev,) if dev is not None else "    o",
                format_float(d) if d is not None else "   o.ooo",
              ) for r, d, dev in brspeeds)
            ))

  results = []
  benchmark_branch = start_branch if start_branch is not None else branches[0]
  # for b in branches:
  for b in [end_branch]:
    for cc in compilers:
      for c in corpuses:
        for f in fs:
          for cl in clevels:
            for s, exp in (data.get(b, {})
                               .get(cc, {})
                               .get(c, {})
                               .get(f, {})
                               .get(cl, {})
                               .items()):
              ctl = (data.get(benchmark_branch, {})
                         .get(cc, {})
                         .get(c, {})
                         .get(f, {})
                         .get(cl, {})
                         .get(s, None))
              if exp and ctl:
                d = VAL_REDUCER(exp) / VAL_REDUCER(ctl)
                # d = max(exp) / max(ctl)
                # d = sum(exp) / sum(ctl) * len(ctl) / len(exp)
                # print("%7s %7s %29s @ %8d: %8.3f%%" % (b, c, f, s, (d - 1) * 100))
                results.append((d, VAL_REDUCER(exp), VAL_REDUCER(ctl), b, cc, c, f, cl, s))

  for d, exp, ctl, b, cc, c, f, cl, s in sorted(results):
    if s >= 256:
      print("%7s on %9s %7s %32s @ %2d on %8d: %s%% (%8.3f vs %8.3f)" % (b, cc, c, f, cl, s, format_float((d - 1) * 100), exp, ctl))

  if os.path.exists(WEIGHTS_FILE):
    length_weights = {}
    for l in open(WEIGHTS_FILE).readlines():
      length, weight = map(int, l.rstrip("\n").split(" "))
      length_log = int(math.floor(math.log(length, 2)))
      # print(length, length_log, weight)
      length_weights.setdefault(2 ** length_log, 0)
      length_weights[2 ** length_log] += length * weight

    # for k, v in sorted(length_weights.items()):
    #   print(k, v)

    total_times = {}
    for b in branches:
      for cc in compilers:
        for f in fs:
          for c in corpuses:
            for cl in clevels:
              total_time = 0
              for s, exp in (data.get(b, {})
                                 .get(cc, {})
                                 .get(c, {})
                                 .get(f, {})
                                 .get(cl, {})
                                 .items()):
                speed = VAL_REDUCER(exp)
                bytes_at_size = length_weights.get(s, 0)
                time_for_size = bytes_at_size / speed / 1024 / 1024
                total_time += time_for_size
                # if bytes_at_size != 0:
                #   print("%7s on %5s %7s %29s @ %8d: %8.3fMB/s on %15dB is %8d sec" % (b, cc, c, f, s, speed, bytes_at_size, time_for_size))
              total_times[(b, cc, f, c, cl)] = total_time

    for cl in clevels:
      for f in fs:
        for c in corpuses:
          for cc in compilers:
            for b in branches:
              exp_time = total_times[(b, cc, f, c, cl)]
              ctl_time = total_times[(benchmark_branch, cc, f, c, cl)]
              print("%7s on %9s %7s %32s @ %2d: %11d sec%s" % (
                b,
                cc,
                c,
                f,
                cl,
                exp_time,
                (" (%8.3f%%)" % (100. * exp_time / ctl_time - 100,) if b != benchmark_branch else "")
              ))


def main():
  sources = []
  corpuses = (
    # "dickens",
    # # "enwik9",
    # "mozilla",
    # "mr",
    # "nci",
    # "ooffice",
    "osdb",
    # "reymont",
    # "samba",
    # "sao",
    # "webster",
    # "xml",
    # "x-ray"
  )
  compilers = (
    # "clang",
    "clang-7.0",
    # "gcc",
    # "gcc-6.4",
    # "gcc-7.2",
    # "gcc",
    # "clang-4.0",
  )
  # clevels = list(range(-100,100))
  # clevels = list(range(1,22))
  clevels = [1, 3, 9, 18]

  branches = BRANCHES
  start_branch = branches[0]
  end_branch = branches[-1]
  colors = {}
  for compiler in compilers:
    for corpus in corpuses:
      for branch in branches:
        fn = os.path.join(
          DATA_DIR,
          "data-%s-%s-%s" % (corpus, branch, compiler)
        )
        if os.path.exists(fn):
          sources.append(Source(
            "%s:%s:%s" % (branch, corpus, compiler),
            fn,
            branch,
            compiler,
            corpus,
            colors.get(branch, "gray"),
          ))


  yranges = [s.yrange() for s in sources]
  xranges = [s.xrange() for s in sources]

  # min_x = min(r[0] for r in xranges + [(X_MIN, X_MAX)])
  # max_x = max(r[1] for r in xranges + [(X_MIN, X_MAX)])
  # min_y = min(r[0] for r in yranges + [(Y_MIN, Y_MAX)])
  # max_y = max(r[1] for r in yranges + [(Y_MIN, Y_MAX)])

  min_x = X_MIN
  max_x = X_MAX
  min_y = Y_MIN
  max_y = Y_MAX

  fs = (
      # "LZ4_compress_default",
      # "LZ4_compress_fast_extState",
      # "LZ4_compress_HC",
      # "LZ4_compress_HC_extStateHC",
      # "LZ4F_compressBegin",
      # "LZ4F_compressFrame",
      # "LZ4F_compressBegin_usingCDict",
      # "LZ4F_compressFrame_usingCDict",
      "ZSTD_compress",
      "ZSTD_compressCCtx",
      "ZSTD_compress_usingCDict",
  )

  if not os.path.exists(GEN_DIR):
    os.mkdir(GEN_DIR)

  data = {}
  for src in sources:
    for s in src.all_series():
      sizes = data.setdefault(s.branch(), {}) \
                  .setdefault(s.compiler(), {}) \
                  .setdefault(s.corpus(), {}) \
                  .setdefault(s.function(), {}) \
                  .setdefault(s.clevel(), {})
      for size, speeds in s.data().items():
        sizes.setdefault(size, []).extend(speeds)

  # for brkey, brdict in sorted(data.items()):
  #   for cckey, ccdict in sorted(brdict.items()):
  #     for ckey, cdict in sorted(ccdict.items()):
  #       for fkey, fdict in sorted(cdict.items()):
  #         for clkey, cleveldict in sorted(fdict.items()):
  #           for size, speeds in sorted(cleveldict.items()):
  #             print(brkey, cckey, ckey, fkey, clkey, size, speeds)
  # return

  branch_pairs = []
  # if start_branch is not None and end_branch is not None:
  #   branch_pairs.append((start_branch, end_branch))
  branch_pairs.extend(zip(branches[:-1], branches[1:]))
  # branch_pairs.extend((branches[0], br) for br in branches[0:])
  # branch_pairs.extend((branches[0], br) for br in branches[1:])
  # branch_pairs.extend(BRANCH_PAIRS)

  # print_stats(branches, branch_pairs, start_branch, end_branch, compilers, corpuses, fs, clevels, data)
  # return

  montage_prefix_and_plots = []

  sources_by_b_cc_c = {}
  for src in sources:
    sources_by_b_cc_c[(src.branch(), src.compiler(), src.corpus())] = src

  # for corpus in ["all"]:
  # for corpus in ["all"] + list(corpuses):
  for corpus in list(corpuses):
    for cc in compilers:
      for brnum, (br1, br2) in enumerate(branch_pairs):
        prefixes_and_plots = []
        for f in fs:
          # branches_to_use = [start_branch, end_branch, br1, br2]
          branches_to_use = [br1, br2]
          corpuses_to_use = corpuses if corpus == "all" else (corpus,)
          clevels_to_use = clevels
          sources_to_use = [
            sources_by_b_cc_c[(b, cc, c)]
            for c in corpuses_to_use
            for b in branches_to_use
          ]
          # series_names_to_use = [
          #   "%s:%s:%s" % (b, c, cc)
          #   for c in corpuses_to_use
          #   for b in branches_to_use
          # ]
          # series = [s.series(f, f, cl) for s in sources for name in series_names_to_use if s.name() == name]

          series = [src.series(f, f, cl) for src in sources_to_use for cl in clevels_to_use]

          plot = gen_new_plot(
            "%s" % (f.replace("_", r"\_")),
            series,
            {},
            None,
            None,
          )

          # plot = gen_plot(
          #   series,
          #   # "%s (%s): %s ({\\tt %s} $\\to$ {\\tt %s}) " % (f.replace("_", r"\_"), cc, corpus, BR_LABELS.get(br1, br1), BR_LABELS.get(br2, br2)),
          #   "%s" % (f.replace("_", r"\_")),
          #   # None,
          #   min_x, max_x, min_y, max_y,
          # )

          prefix = "%s-%s-%02d-%s-%s-%s-all-cls" % (corpus, cc, brnum, br1, br2, f)
          prefixes_and_plots.append((prefix, plot))
        montage_prefix = "%s-%s-%02d-%s-%s-all-cls" % (corpus, cc, brnum, br1, br2)
        montage_prefix_and_plots.append((montage_prefix, prefixes_and_plots))


  # for corpus in list(corpuses):
  #   for cc in compilers:
  #     prefixes_and_plots = []
  #     for f in fs:
  #       # branches_to_use = [start_branch, end_branch, br1, br2]
  #       branches_to_use = branches
  #       corpuses_to_use = corpuses if corpus == "all" else (corpus,)
  #       clevels_to_use = clevels
  #       sources_to_use = [
  #         sources_by_b_cc_c[(b, cc, c)]
  #         for c in corpuses_to_use
  #         for b in branches_to_use
  #       ]
  #       # series_names_to_use = [
  #       #   "%s:%s:%s" % (b, c, cc)
  #       #   for c in corpuses_to_use
  #       #   for b in branches_to_use
  #       # ]
  #       # series = [s.series(f, f, cl) for s in sources for name in series_names_to_use if s.name() == name]

  #       series = [src.series(f, f, cl) for src in sources_to_use for cl in clevels_to_use]

  #       series_colors = {}

  #       # colors = ["red!50!gray", "blue!50!gray", "green!50!gray"]
  #       colors = ["red!50!gray", "green!50!gray"]
  #       for color, br in zip(colors, branches):
  #         series_colors.update({"%s:%s:%s:%s:%s" % (br, c, cc, f, cl): color for c in corpuses_to_use for cl in clevels_to_use})

  #       for s in series:
  #         s._color = series_colors.get(s.name(), s._color)

  #       plot = gen_plot(
  #         series,
  #         "%s (%s): %s ({\\tt %s} $\\to$ {\\tt %s}) " % (f.replace("_", r"\_"), cc, corpus, BR_LABELS.get(branches[0], branches[0]), BR_LABELS.get(branches[-1], branches[-1])),
  #         min_x, max_x, min_y, max_y,
  #       )

  #       prefix = "%s-%s-all-brs-%s-all-cls" % (corpus, cc, f)
  #       prefixes_and_plots.append((prefix, plot))
  #     montage_prefix = "%s-%s-all-brs-all-cls" % (corpus, cc)
  #     montage_prefix_and_plots.append((montage_prefix, prefixes_and_plots))


  # for corpus in ["all"]:
  # # for corpus in ["all"] + list(corpuses):
  # # for corpus in list(corpuses):
  #   for cc in compilers:
  #     for cl in clevels:
  #       for brnum, (br1, br2) in enumerate(branch_pairs):
  #         prefixes_and_plots = []
  #         for f in fs:
  #           # branches_to_use = [start_branch, end_branch, br1, br2]
  #           branches_to_use = [br1, br2]
  #           corpuses_to_use = corpuses if corpus == "all" else (corpus,)
  #           sources_to_use = [
  #             sources_by_b_cc_c[(b, cc, c)]
  #             for c in corpuses_to_use
  #             for b in branches_to_use
  #           ]
  #           # series_names_to_use = [
  #           #   "%s:%s:%s" % (b, c, cc)
  #           #   for c in corpuses_to_use
  #           #   for b in branches_to_use
  #           # ]
  #           # series = [s.series(f, f, cl) for s in sources for name in series_names_to_use if s.name() == name]

  #           series = [src.series(f, f, cl) for src in sources_to_use]

  #           series_colors = {}
  #           series_colors.update({"%s:%s:%s:%s:%s" % (start_branch, c, cc, f, cl): "red!25!gray, opacity=.1" for c in corpuses_to_use})
  #           series_colors.update({"%s:%s:%s:%s:%s" % (end_branch, c, cc, f, cl): "green!25!gray, opacity=.1" for c in corpuses_to_use})
  #           series_colors.update({"%s:%s:%s:%s:%s" % (br1, c, cc, f, cl): "red!50!gray" for c in corpuses_to_use})
  #           series_colors.update({"%s:%s:%s:%s:%s" % (br2, c, cc, f, cl): "green!50!gray" for c in corpuses_to_use})
  #           for s in series:
  #             s._color = series_colors.get(s.name(), s._color)

  #           plot = gen_plot(
  #             series,
  #             "%s (%s): %s ({\\tt %s} $\\to$ {\\tt %s}) " % (f.replace("_", r"\_"), cc, corpus, br1, br2),
  #             min_x, max_x, min_y, max_y,
  #           )

  #           prefix = "%s-%s-%02d-%s-%s-%s-%02d" % (corpus, cc, brnum, br1, br2, f, cl)
  #           prefixes_and_plots.append((prefix, plot))
  #         montage_prefix = "%s-%s-%02d-%s-%s-%02d" % (corpus, cc, brnum, br1, br2, cl)
  #         montage_prefix_and_plots.append((montage_prefix, prefixes_and_plots))

  for montage_prefix, prefixes_and_plots in montage_prefix_and_plots:
      png_files = []
      pdflatex_cmds = []
      convert_cmds = []
      for prefix, plot in prefixes_and_plots:
        set_up_render(prefix, plot, png_files, pdflatex_cmds, convert_cmds)
      run_renders(pdflatex_cmds, convert_cmds)
      montage(png_files, montage_prefix)

def format_float(n, lp=2, tp=3):
  s = "%*.*f" % (lp + tp + 2, tp, n)
  if n == 0:
    return s
  color = "\033[32m" if n > 0 else "\033[31m"
  for i, c in enumerate(s):
    if c not in " -0.":
      return s[:i] + color + s[i:] + "\033[0m"
  return s


class Series(object):
  def __init__(self, name, branch, compiler, corpus, function, clevel, data, color, thickness):
    self._name = name
    self._branch = branch
    self._compiler = compiler
    self._corpus = corpus
    self._function = function
    self._clevel = clevel
    self._data = data
    self._color = color
    self._thickness = thickness

  def name(self):
    return self._name

  def branch(self):
    return self._branch

  def compiler(self):
    return self._compiler

  def corpus(self):
    return self._corpus

  def function(self):
    return self._function

  def clevel(self):
    return self._clevel

  def data(self):
    return self._data

  def color(self):
    return self._color

  def thickness(self):
    return self._thickness

  def yrange(self):
    return (min(VAL_REDUCER(v) for v in self._data.values()), max(VAL_REDUCER(v) for v in self._data.values()))

  def xrange(self):
    return (min(self._data.keys()), max(self._data.keys()))


class Source(object):
  def __init__(self, name, filename, branch, compiler, corpus, color="gray", thickness="thick"):
    self._name = name
    self._filename = filename
    self._branch = branch
    self._compiler = compiler
    self._corpus = corpus
    self._color = color
    self._thickness = thickness

    self.load()

  def load(self):
    self._data = {}
    input_size = 0
    if os.path.exists(self._filename):
      for l in open(self._filename).readlines():
        # m = INPUT_SIZE_RE.match(l)
        # if m:
        #   input_size = int(m.group(1))
        m = RUN_RE.match(l)
        if m and int(m.group("bytes_in")) >= MIN_INPUT_SIZE:
          self._data \
              .setdefault(m.group("function"), {}) \
              .setdefault(int(m.group("clevel")), {}) \
              .setdefault(int(m.group("bytes_in")), []) \
              .append(float(m.group("speed")))

  def name(self):
    return self._name

  def branch(self):
    return self._branch

  def compiler(self):
    return self._compiler

  def corpus(self):
    return self._corpus

  def series(self, name, function, clevel):
    return Series(
        self._name + ":" + name + ":" + str(clevel),
        self._branch,
        self._compiler,
        self._corpus,
        function,
        clevel,
        self._data.get(function, {}).get(clevel, {}),
        self._color,
        self._thickness)

  def all_series(self):
    series = []
    for function, clevels in self._data.items():
      for clevel, data in clevels.items():
        series.append(Series(
            self._name + ":" + function + ":" + str(clevel),
            self._branch,
            self._compiler,
            self._corpus,
            function,
            clevel,
            data,
            self._color,
            self._thickness))
    return series

  def yrange(self):
    mins = []
    maxs = []
    for series in self.all_series():
      yr = series.yrange()
      if yr is not None and yr[0] is not None:
        mins.append(yr[0])
      if yr is not None and yr[1] is not None:
        maxs.append(yr[1])
    return (
      min(mins) if mins else None,
      max(maxs) if maxs else None
    )

  def xrange(self):
    mins = []
    maxs = []
    for series in self.all_series():
      xr = series.xrange()
      if xr is not None and xr[0] is not None:
        mins.append(xr[0])
      if xr is not None and xr[1] is not None:
        maxs.append(xr[1])
    return (
      min(mins) if mins else None,
      max(maxs) if maxs else None
    )


def gen_plot(series, title, min_x, max_x, min_y, max_y, log_x=True, log_y=True):
  if log_x:
    xp2v = log_xp2v
    xv2p = log_xv2p
  else:
    xp2v = lin_xp2v
    xv2p = lin_xv2p

  if log_y:
    yp2v = log_yp2v
    yv2p = log_yv2p
  else:
    yp2v = lin_yp2v
    yv2p = lin_yv2p

  min_x = xp2v(math.floor(min(xv2p(min_x), xv2p(X_MIN))))
  max_x = xp2v(math.ceil (max(xv2p(max_x), xv2p(X_MAX))))
  min_y = yp2v(math.floor(min(yv2p(min_y), yv2p(Y_MIN))))
  max_y = yp2v(math.ceil (max(yv2p(max_y), yv2p(Y_MAX))))

  x_lines = [X_INC ** i for i in range(int(math.log(min_x, X_INC)), int(math.log(max_x, X_INC)) + 1, 2)]
  y_lines = [Y_INC ** (i / Y_SCALE) for i in range(int(math.log(min_y, Y_INC) * Y_SCALE), int(math.log(max_y, Y_INC) * Y_SCALE) + 1, 2)]

  ls = []

  ls.append(r"\documentclass{standalone}")
  ls.append(r"\usepackage[sfdefault,light]{roboto}")
  ls.append(r"\usepackage{tikz}")
  ls.append(r"\usepackage{graphicx}")
  ls.append(r"\begin{document}")
  ls.append(r"\begin{tikzpicture}")

  for xv in x_lines:
    ls.append("\\draw [opacity=.1] (%f, %f) -- (%f, %f);" % (xv2p(xv), yv2p(min_y) - .1, xv2p(xv), yv2p(max_y)))
    ls.append("\\node [anchor=west, rotate=-60] at (%f, %f) {%s};" % (xv2p(xv), yv2p(min_y) - .1, X_AXIS_LABELS.get(xv, "")));

  for yv in y_lines:
    ls.append("\\draw [opacity=.1] (%f, %f) -- (%f, %f);" % (xv2p(min_x) - .1, yv2p(yv), xv2p(max_x), yv2p(yv)))
    ls.append("\\node [anchor=east] at (%f, %f) {%s};" % (xv2p(min_x) - .1, yv2p(yv), Y_AXIS_LABELS.get(yv, "")));

  if max_x > 0:
    ls.append("\\draw [->] (%f, %f) -- (%f, %f);" % (0, 0, xv2p(max_x) + .5, 0))
  if min_x < 0:
    ls.append("\\draw [->] (%f, %f) -- (%f, %f);" % (0, 0, xv2p(max_x) - .0, 0))
  if yv2p(max_y) > 0:
    ls.append("\\draw [->] (%f, %f) -- (%f, %f);" % (0, 0, 0, yv2p(max_y) + .5))
  if yv2p(min_y) < 0:
    ls.append("\\draw [->] (%f, %f) -- (%f, %f);" % (0, 0, 0, yv2p(min_y) - .0))

  ls.append("\\node at (%f, %f) {%s};" % ((xv2p(min_x) + xv2p(max_x)) / 2., yv2p(min_y) - 1.5, "\\large Uncompressed Size"))
  ls.append("\\node [rotate=90] at (%f, %f) {%s};" % (xv2p(min_x) - 1.5, (yv2p(min_y) + yv2p(max_y)) / 2., "\\large MB/s"))

  if title is not None:
    ls.append("\\node [anchor=south] at (%f, %f) {%s};" % ((xv2p(min_x) + xv2p(max_x)) / 2., yv2p(max_y) + .5, "\\LARGE " + title))


  for s in series:
    # ls.append(r"\draw [%s, %s, fill=%s, opacity=.25]" % (s.color(), s.thickness(), s.color()))
    # first = True
    # kvs = sorted(s.data().items())
    # for k, v in kvs:
    #   x = xv2p(k)
    #   y = yv2p(max(v))
    #   ls.append("%s(%.3f, %.3f)" % ("" if first else "-- ", x, y))
    #   first = False
    # kvs.reverse()
    # for k, v in kvs:
    #   x = xv2p(k)
    #   y = yv2p(min(v))
    #   ls.append("%s(%.3f, %.3f)" % ("" if first else "-- ", x, y))
    # ls.append("-- cycle")

    # ls.append(r";")

    ls.append(r"\draw [%s, %s]" % (s.color(), s.thickness()))
    first = True
    for k, v in sorted(s.data().items()):
      v = VAL_REDUCER(v)
      if v < min_y:
        v = min_y
      if v > max_y:
        v = max_y
      if v >= min_y and v <= max_y and k >= min_x and k <= max_x:
        x = xv2p(k)
        y = yv2p(v)
        ls.append("%s(%.3f, %.3f)" % ("" if first else "-- ", x, y))
        first = False

    ls.append(r";")


  ls.append(r"\end{tikzpicture}")
  ls.append(r"\end{document}")

  return "\n".join(ls)


def gen_new_plot(title, new_speeds, old_speeds, new_ratios, old_ratios):
  x_axis = graph.LogAxis(2)
  x_axis.set_title("Uncompressed Size")
  x_axis.set_marks(graph.ManualLogMarks())
  x_axis.set_pos(0)
  x_axis.set_size(16)

  speed_plot = graph.Plot()
  speed_plot.set_title(title)

  speed_plot.set_x_axis(x_axis)

  speed_y_axis = graph.LogAxis(2)
  speed_plot.set_y_axis(speed_y_axis)
  speed_plot.set_height(16)
  speed_plot.set_y_pos(0)

  for speeds_dict, color in (
    (old_speeds, "red!50!black"),
    (new_speeds, "green!50!black"),
  ):
    for series_name, speeds in speeds_dict:
      data = { k: VAL_REDUCER(v) for k, v in speeds.data().items() }
      s = graph.Series(data)
      s.set_line_style(color)
      speed_plot.add_series(s)

  d = graph.Document([speed_plot])


def set_up_render(prefix, plot, png_files, pdflatex_cmds, convert_cmds):
  tex_file = "%s.tex" % (prefix)
  pdf_file = "%s.pdf" % (prefix)
  png_file = "%s.png" % (prefix)
  png_files.append(png_file)

  open(os.path.join(GEN_DIR, tex_file), "w").write(plot)

  pdflatex_cmd = ["pdflatex", tex_file]
  pdflatex_cmds.append(pdflatex_cmd)

  convert_cmd = [
    "convert",
    "-verbose",
    "-density", "100",
    pdf_file,
    "-quality", "100",
    "-sharpen", "0x1.0",
    "-flatten",
    png_file,
  ]
  convert_cmds.append(convert_cmd)

def run_renders(pdflatex_cmds, convert_cmds):
  procs = [subprocess.Popen(cmd, cwd=GEN_DIR, stdin=DEVNULL, stdout=DEVNULL, stderr=DEVNULL) for cmd in pdflatex_cmds]
  for proc in procs:
    proc.wait()
  procs = [subprocess.Popen(cmd, cwd=GEN_DIR, stdin=DEVNULL, stdout=DEVNULL, stderr=DEVNULL) for cmd in convert_cmds]
  for proc in procs:
    proc.wait()

def montage(png_files, prefix):
  montage_cmd = [
    "montage",
    "-verbose",
  ] + png_files + [
    "-quality", "100",
    "-sharpen", "0x1.0",
    "-geometry", "1x1<+50+50",
    "-tile", "x1",
    "montage-%s.png" % (prefix),
  ]
  print(" ".join(montage_cmd))
  subprocess.call(montage_cmd, cwd=GEN_DIR, stdout=DEVNULL)

if __name__ == '__main__':
  main()
