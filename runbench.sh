# g rev-list --abbrev-commit dev..HEAD | tr '\n' ' '

export BRANCHES="c32e031 b2637ab 62d7cdc 5b67c7d 6c23f03 dfed9fa"

export PROGDIR="/home/felix/prog"

export BENCHDIR="$PROGDIR/compressor-benchmark"
export LZ4DIR="$PROGDIR/lz4"
export SILESIADIR="$PROGDIR/silesia"
export BINDIR="$BENCHDIR/bench/bin"
export LOGSDIR="$BENCHDIR/bench/data"
export TMPDIR="$BENCHDIR/bench/tmp"

export COMPILERS="gcc clang-4.0"
export EXENAME="framebench-lz4"

export CORPUSES="$(ls $SILESIADIR)"
# export SIZES="$(python -c 'print(" ".join(str(3 * 2 ** ((i - 3) / 2) if i % 2 else 2 ** (i / 2)) for i in range(12, 41)))')"
export SIZES="$(python -c 'print(" ".join(str(3 * 2 ** ((i - 3) / 2) if i % 2 else 2 ** (i / 2)) for i in range(6, 59)))')"

# mkdir -p $BINDIR $LOGSDIR $TMPDIR

for BRANCH in $BRANCHES; do
  for COMPILER in $COMPILERS; do
    echo $BRANCH $COMPILER
    if [ ! -e $BINDIR/$EXENAME-$BRANCH-$COMPILER ]; then
      if (cd $LZ4DIR; git co $BRANCH); then
        if make -C $LZ4DIR clean && \
           make -C $BENCHDIR clean && \
           make -C $BENCHDIR $EXENAME -j32 CC=$COMPILER MOREFLAGS="-O3 -march=native -mtune=native -DBENCH_LZ4_COMPRESSFRAME_USINGCDICT_TAKES_CCTX -DBENCH_LZ4_HAS_FASTRESET" || \
           make -C $BENCHDIR $EXENAME -j32 CC=$COMPILER MOREFLAGS="-O3 -march=native -mtune=native"; then
          mv $BENCHDIR/$EXENAME $BINDIR/$EXENAME-$BRANCH-$COMPILER
        else
          make -C $LZ4DIR clean
          make -C $BENCHDIR clean
        fi
      fi
    fi
  done
done

for i in $(seq 1000); do
  for SIZE in $SIZES; do
    for CORPUS in $CORPUSES; do
      export CF=$SILESIADIR/$CORPUS
      export DICT=$TMPDIR/$CORPUS-dict
      if [ ! -e $DICT ]; then
        tail -c 65536 $CF > $DICT
      fi
      if [ "$(wc -c < $CF)" -ge "$SIZE" ]; then
        if [ ! -e $TMPDIR/$CORPUS-in-$SIZE ]; then
          head -c $SIZE $CF > $TMPDIR/$CORPUS-in-$SIZE
        fi
        for COMPILER in $(echo $COMPILERS | tr ' ' '\n' | shuf | tr '\n' ' '); do
          for BRANCH in $(echo $BRANCHES | tr ' ' '\n' | shuf | tr '\n' ' '); do
            echo $CORPUS $SIZE $COMPILER $BRANCH
            $BINDIR/$EXENAME-$BRANCH-$COMPILER $BRANCH-$COMPILER $DICT $TMPDIR/$CORPUS-in-$SIZE |& \
              tee -a $LOGSDIR/data-$CORPUS-$BRANCH-$COMPILER &
          done
          wait
        done
      fi
    done
  done
done
