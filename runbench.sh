# g rev-list --abbrev-commit dev..ref-dict-table-test-22 | tr '\n' ' '

# export BRANCHES="d203a72 01be2e3 987dfc2 bac7f3d b8f143e 1ac0b6c e56b501"
# export BRANCHES="8aa4578 50d3ed6 52ac9f2 0cecaf5 553b7ac 4d94029"
export BRANCHES="bb3fa11 6c0e0f3 824d15e 9ad15ee a3bb38b 44504aa 05e3489"

export LZ4DIR="/data/users/felixh/lz4"
# export LZ4DIR="/home/felixh/prog/lz4"
export TESTSDIR="$LZ4DIR/tests"
export BINDIR="$TESTSDIR/bench/bin"
export LOGSDIR="/$TESTSDIR/bench/data"
export TMPDIR="$TESTSDIR/bench/tmp"
export SILESIADIR="/data/users/felixh/silesia"
# export SILESIADIR="/home/felix/prog/silesia"
export COMPILERS="gcc clang"

# mkdir -p $BINDIR $LOGSDIR $TMPDIR

for BRANCH in $BRANCHES; do
  for COMPILER in $COMPILERS; do
    if [ ! -e $BINDIR/framebench-$BRANCH-$COMPILER ]; then
      if (cd $TESTSDIR; git co $BRANCH); then
        make -C $TESTSDIR/.. clean
        CC=$COMPILER make -C $TESTSDIR -j32 framebench MOREFLAGS="-O3 -march=native -mtune=native"
        mv $TESTSDIR/framebench $BINDIR/framebench-$BRANCH-$COMPILER
      fi
    fi
  done
done

export CORPUSES="$(ls $SILESIADIR)"
# export SIZES="$(python -c 'print(" ".join(str(3 * 2 ** ((i - 3) / 2) if i % 2 else 2 ** (i / 2)) for i in range(12, 41)))')"
export SIZES="$(python -c 'print(" ".join(str(3 * 2 ** ((i - 3) / 2) if i % 2 else 2 ** (i / 2)) for i in range(6, 59)))')"

for i in $(seq 1000); do
  for SIZE in $SIZES; do
    for CORPUS in $CORPUSES; do
      export CF=$SILESIADIR/$CORPUS
      export DICT=$TMPDIR/tmp-$CORPUS-dict
      if [ ! -e $DICT ]; then
        tail -c 65536 $CF > $DICT
      fi
      if [ "$(wc -c < $CF)" -ge "$SIZE" ]; then
        if [ ! -e $TMPDIR/tmp-$CORPUS-in-$SIZE ]; then
          head -c $SIZE $CF > $TMPDIR/tmp-$CORPUS-in-$SIZE
        fi
        for COMPILER in $(echo $COMPILERS | tr ' ' '\n' | shuf | tr '\n' ' '); do
          for BRANCH in $(echo $BRANCHES | tr ' ' '\n' | shuf | tr '\n' ' '); do
            echo $CORPUS $SIZE $COMPILER $BRANCH
            $BINDIR/framebench-$BRANCH-$COMPILER $BRANCH-$COMPILER $DICT $TMPDIR/tmp-$CORPUS-in-$SIZE |& \
              tee -a $LOGSDIR/data-$CORPUS-$BRANCH-$COMPILER &
          done
        done
        wait
      fi
    done
  done
done
