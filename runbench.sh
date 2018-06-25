# g rev-list --abbrev-commit dev..HEAD | tr '\n' ' '

# export BRANCHES="cc5c9fe 8553c3b 4d50a9f 5a6c2c8 95a1596 97fed66 b095bff"
export BRANCHES="3bc57db 1f86f20 dbf373a b095bff"

export PROGDIR="/home/felixh/prog"

export BENCHDIR="$PROGDIR/compressor-benchmark"
export LZ4DIR="$PROGDIR/lz4"
export ZSTDDIR="$PROGDIR/zstd"
export SILESIADIR="$PROGDIR/silesia"
export BINDIR="$BENCHDIR/bench/bin"
export LOGSDIR="$BENCHDIR/bench/data"
export TMPDIR="$BENCHDIR/bench/tmp"

export MOREFLAGS="-O3 -march=native -mtune=native"
# export MOREFLAGS="-Og -ggdb"
# export MOREFLAGS="-DZSTD_DEBUG=6 -DBENCH_INITIAL_REPETITIONS=2 -DBENCH_TARGET_NANOSEC=1 -DBENCH_DONT_RANDOMIZE_INPUT -Og -ggdb"

export COMPILERS="gcc clang-4.0"
# export COMPILERS="gcc"
# export EXENAME="framebench-lz4"
export EXENAME="framebench-zstd"

export MIN_CLEVEL=3
export MAX_CLEVEL=15

export CORPUSES="$(ls $SILESIADIR)"
export SIZES="$(python -c 'print(" ".join(str(3 * 2 ** ((i - 3) / 2) if i % 2 else 2 ** (i / 2)) for i in range(12, 41)))')"
# export SIZES="$(python -c 'print(" ".join(str(3 * 2 ** ((i - 3) / 2) if i % 2 else 2 ** (i / 2)) for i in range(6, 59)))')"

mkdir -p $BINDIR $LOGSDIR $TMPDIR

for BRANCH in $BRANCHES; do
  for COMPILER in $COMPILERS; do
    echo $BRANCH $COMPILER
    if [ ! -e $BINDIR/$EXENAME-$BRANCH-$COMPILER ]; then
      if (cd $ZSTDDIR; git co $BRANCH); then
        if make -C $ZSTDDIR clean && \
           make -C $BENCHDIR clean && \
           # make -C $BENCHDIR $EXENAME -j32 CC=$COMPILER MOREFLAGS="-O3 -march=native -mtune=native -DBENCH_LZ4_COMPRESSFRAME_USINGCDICT_TAKES_CCTX -DBENCH_LZ4_HAS_FASTRESET" || \
           make -C $BENCHDIR $EXENAME -j32 CC=$COMPILER MOREFLAGS="$MOREFLAGS"; then
          mv $BENCHDIR/$EXENAME $BINDIR/$EXENAME-$BRANCH-$COMPILER
        else
          make -C $ZSTDDIR clean
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
      export DICT=$BENCHDIR/bench/dicts/$CORPUS.zstd-dict
      if [ ! -e $DICT ]; then
        export DICT=$TMPDIR/$CORPUS-dict
        if [ ! -e $DICT ]; then
          tail -c 65536 $CF > $DICT
        fi
      fi
      if [ "$(wc -c < $CF)" -ge "$SIZE" ]; then
        if [ ! -e $TMPDIR/$CORPUS-in-$SIZE ]; then
          head -c $SIZE $CF > $TMPDIR/$CORPUS-in-$SIZE
        fi
        for COMPILER in $(echo $COMPILERS); do
          for BRANCH in $(echo $BRANCHES); do
            echo $CORPUS $SIZE $COMPILER $BRANCH
            $BINDIR/$EXENAME-$BRANCH-$COMPILER -l $BRANCH-$COMPILER -D $DICT -i $TMPDIR/$CORPUS-in-$SIZE -b $MIN_CLEVEL -e $MAX_CLEVEL |& \
              tee -a $LOGSDIR/data-$CORPUS-$BRANCH-$COMPILER &
          done
        done
        wait
      fi
    done
  done
done
