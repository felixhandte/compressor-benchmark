#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef BENCH_LZ4
#define LZ4_STATIC_LINKING_ONLY
#define LZ4_HC_STATIC_LINKING_ONLY
#define LZ4F_STATIC_LINKING_ONLY
#include "lz4.h"
#include "lz4hc.h"
#include "lz4frame.h"
#endif

#ifdef BENCH_ZSTD
#include "zstd.h"
#endif

#define CHECK_R(err, ...) do { if (err) { \
  fprintf(stderr, "%s:%s:%d: ", __FILE__, __FUNCTION__, __LINE__); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n"); \
  return -1; \
} } while(0);

#define CHECK(err, ...) do { if (err) { \
  fprintf(stderr, "%s:%s:%d: ", __FILE__, __FUNCTION__, __LINE__); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n"); \
  raise(SIGABRT); \
} } while(0);

#ifdef BENCH_LZ4
#define LZ4F_CHECK_R(err, ret) do { typeof(err) _err = (err); if (LZ4F_isError(_err)) { fprintf(stderr, "Error!: %s\n", LZ4F_getErrorName(_err)); return (ret); } } while(0);

#define LZ4F_CHECK(err) LZ4F_CHECK_R(err, 0)
#endif

#ifndef BENCH_INITIAL_REPETITIONS
#define BENCH_INITIAL_REPETITIONS 4ull
#endif

#ifndef BENCH_STARTING_ITER
#define BENCH_STARTING_ITER 0ull
#endif

#ifndef BENCH_TARGET_NANOSEC
#define BENCH_TARGET_NANOSEC (25ull * 1000 * 1000)
#endif

#ifndef BENCH_DONT_RANDOMIZE_INPUT
#ifndef BENCH_RANDOMIZE_INPUT
#define BENCH_RANDOMIZE_INPUT
#endif
#endif


typedef struct {
  int print_help;
  int min_clevel;
  int max_clevel;
  char *prog_name;
  char *run_name;
  int bench_zstd;
  int bench_lz4;
  char *in_fn;
  char *dict_fn;
  size_t target_nanosec;
  size_t initial_reps;
  size_t starting_iter;
} args_t;

typedef struct {
  const char *run_name;
  size_t iter;
#ifdef BENCH_LZ4
  LZ4_stream_t *ctx;
  LZ4_streamHC_t *hcctx;
  LZ4_stream_t *dictctx;
  LZ4_streamHC_t *dicthcctx;
  LZ4F_cctx *cctx;
  LZ4F_dctx *dctx;
  const LZ4F_CDict* cdict;
  LZ4F_preferences_t* prefs;
  const LZ4F_compressOptions_t* options;
#endif
#ifdef BENCH_ZSTD
  ZSTD_CCtx *zcctx;
  ZSTD_DCtx *zdctx;
  ZSTD_CDict **zcdicts;
  ZSTD_DDict *zddict;
#endif
  const char *dictbuf;
  size_t dictsize;
  char *obuf;
  size_t osize;
  const char* ibuf;
  const char* isample;
  size_t isize;
  size_t num_ibuf;
  char *checkbuf;
  size_t checksize;
  int clevel;
} bench_params_t;

#ifdef BENCH_LZ4
size_t compress_frame(bench_params_t *p) {
#ifdef BENCH_LZ4_COMPRESSFRAME_USINGCDICT_TAKES_CCTX
  LZ4F_cctx *cctx = p->cctx;
#endif
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  const LZ4F_CDict* cdict = p->cdict;
  LZ4F_preferences_t* prefs = p->prefs;

  size_t oused;

  prefs->frameInfo.contentSize = isize;

  oused = LZ4F_compressFrame_usingCDict(
#ifdef BENCH_LZ4_COMPRESSFRAME_USINGCDICT_TAKES_CCTX
    cctx,
#endif
    obuf,
    osize,
    isample,
    isize,
    cdict,
    prefs);
  LZ4F_CHECK(oused);

  return oused;
}

size_t compress_begin(bench_params_t *p) {
  LZ4F_cctx *cctx = p->cctx;
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  const LZ4F_CDict* cdict = p->cdict;
  LZ4F_preferences_t* prefs = p->prefs;
  const LZ4F_compressOptions_t* options = p->options;

  char *oend = obuf + osize;
  size_t oused;

  prefs->frameInfo.contentSize = isize;

  oused = LZ4F_compressBegin_usingCDict(cctx, obuf, oend - obuf, cdict, prefs);
  LZ4F_CHECK(oused);
  obuf += oused;
  oused = LZ4F_compressUpdate(
    cctx,
    obuf,
    oend - obuf,
    isample,
    isize,
    options);
  LZ4F_CHECK(oused);
  obuf += oused;
  oused = LZ4F_compressEnd(cctx, obuf, oend - obuf, options);
  LZ4F_CHECK(oused);

  return obuf - p->obuf;
}

size_t compress_default(bench_params_t *p) {
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;

  char *oend = obuf + osize;
  size_t oused;

  oused = LZ4_compress_default(isample, obuf, isize, oend - obuf);
  obuf += oused;

  return obuf - p->obuf;
}

size_t compress_extState(bench_params_t *p) {
  LZ4_stream_t *ctx = p->ctx;
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  char *oend = obuf + osize;
  size_t oused;

#ifdef BENCH_LZ4_HAS_FASTRESET
  oused = LZ4_compress_fast_extState_fastReset(
      ctx, isample, obuf, isize, oend - obuf, clevel);
#else
  oused = LZ4_compress_fast_extState(
      ctx, isample, obuf, isize, oend - obuf, clevel);
#endif
  obuf += oused;

  return obuf - p->obuf;
}

size_t compress_hc(bench_params_t *p) {
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  char *oend = obuf + osize;
  size_t oused;

  oused = LZ4_compress_HC(
      isample, obuf, isize, oend - obuf, clevel);
  obuf += oused;

  return obuf - p->obuf;
}

size_t compress_hc_extState(bench_params_t *p) {
  LZ4_streamHC_t *hcctx = p->hcctx;
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  char *oend = obuf + osize;
  size_t oused;


#ifdef BENCH_LZ4_HAS_FASTRESET
  oused = LZ4_compress_HC_extStateHC_fastReset(
      hcctx, isample, obuf, isize, oend - obuf, clevel);
#else
  oused = LZ4_compress_HC_extStateHC(
      hcctx, isample, obuf, isize, oend - obuf, clevel);
#endif
  obuf += oused;

  return obuf - p->obuf;
}

size_t compress_dict(bench_params_t *p) {
  LZ4_stream_t *ctx = p->ctx;
  const LZ4_stream_t *dictctx = p->dictctx;
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  char *oend = obuf + osize;
  size_t oused;

  LZ4_resetStream_fast(ctx);
  LZ4_attach_dictionary(ctx, dictctx);

  oused = LZ4_compress_fast_continue(
      ctx,
      isample, obuf,
      isize, oend - obuf,
      clevel);
  obuf += oused;

  return obuf - p->obuf;
}

size_t compress_hc_dict(bench_params_t *p) {
  LZ4_streamHC_t *hcctx = p->hcctx;
  const LZ4_streamHC_t *dicthcctx = p->dicthcctx;
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  char *oend = obuf + osize;
  size_t oused;

  LZ4_resetStreamHC_fast(hcctx, clevel);
  LZ4_attach_HC_dictionary(hcctx, dicthcctx);

  oused = LZ4_compress_HC_continue(
      hcctx,
      isample, obuf,
      isize, oend - obuf);
  obuf += oused;

  return obuf - p->obuf;
}
#endif

#ifdef BENCH_ZSTD
size_t zstd_compress_default(bench_params_t *p) {
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  size_t oused;

  oused = ZSTD_compress(obuf, osize, isample, isize, clevel);

  return oused;
}

size_t zstd_compress_cctx(bench_params_t *p) {
  ZSTD_CCtx *ctx = p->zcctx;
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  size_t oused;

  oused = ZSTD_compressCCtx(ctx, obuf, osize, isample, isize, clevel);

  return oused;
}

size_t zstd_compress_cdict(bench_params_t *p) {
  ZSTD_CCtx *ctx = p->zcctx;
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;
  ZSTD_CDict *cdict = p->zcdicts[clevel];

  size_t oused;

  oused = ZSTD_compress_usingCDict(ctx, obuf, osize, isample, isize, cdict);

  return oused;
}
#endif

#ifdef BENCH_LZ4
size_t check_lz4(bench_params_t *p, size_t csize) {
  (void)csize;
  memset(p->checkbuf, 0xFF, p->checksize);
  return LZ4_decompress_fast_usingDict(p->obuf, p->checkbuf, p->isize,
                                       p->dictbuf, p->dictsize)
      && !memcmp(p->isample, p->checkbuf, p->isize);
}

size_t check_lz4f(bench_params_t *p, size_t csize) {
  size_t cp = 0;
  size_t dp = 0;
  size_t dsize = p->checksize;
  size_t cleft = csize;
  size_t dleft = dsize;
  size_t ret;
  memset(p->checkbuf, 0xFF, p->checksize);
  LZ4F_resetDecompressionContext(p->dctx);
  do {
    ret = LZ4F_decompress_usingDict(
        p->dctx, p->checkbuf + dp, &dleft, p->obuf + cp, &cleft,
        p->dictbuf, p->dictsize, NULL);
    cp += cleft;
    dp += dleft;
    cleft = csize - cp;
    dleft = dsize - dp;
    if (LZ4F_isError(ret)) return 0;
  } while (cleft);
  return !memcmp(p->isample, p->checkbuf, p->isize);
}
#endif

#ifdef BENCH_ZSTD
size_t check_zstd(bench_params_t *p, size_t csize) {
  (void)csize;
  memset(p->checkbuf, 0xFF, p->checksize);
  return ZSTD_decompress_usingDDict(p->zdctx, p->checkbuf, p->checksize, p->obuf, csize, p->zddict) == p->isize
      && !memcmp(p->isample, p->checkbuf, p->isize);
}
#endif

uint64_t bench(
    const char *bench_name,
    size_t (*fun)(bench_params_t *),
    size_t (*checkfun)(bench_params_t *, size_t),
    bench_params_t *params,
    const args_t *args
) {
  struct timespec start, end;
  size_t i, osize = 0, o = 0;
  size_t time_taken = 0;
  uint64_t total_repetitions = 0;
  uint64_t repetitions = args->initial_reps;

  if (clock_gettime(CLOCK_MONOTONIC_RAW, &start)) return 0;

  while (total_repetitions == 0 || time_taken < args->target_nanosec) {
    if (total_repetitions) {
        repetitions = total_repetitions; // double previous
    }

    for (i = args->starting_iter; i < args->starting_iter + repetitions; i++) {
      params->iter = i;
      if (params->num_ibuf == 1) {
        params->isample = params->ibuf;
      } else {
#ifdef BENCH_RANDOMIZE_INPUT
        params->isample = params->ibuf + ((i * 2654435761U) % ((params->num_ibuf - 1) * params->isize));
#else
        params->isample = params->ibuf + ((i * 2654435761U) % params->num_ibuf) * params->isize;
#endif
      }
      o = fun(params);
      if (!o) {
        fprintf(
            stderr,
            "%-19s: %-30s @ lvl %3d: %8ld B: FAILED!\n",
            params->run_name, bench_name, params->clevel,
            params->isize);
        return 0;
      }
      // fprintf(
      //     stderr,
      //     "%-19s: %-30s @ lvl %3d: %8ld B -> %8ld B: iter %8ld\n",
      //     params->run_name, bench_name, params->clevel,
      //     params->isize, o, i);
      osize += o;
    }

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &end)) return 0;

    time_taken = (1000 * 1000 * 1000 * end.tv_sec + end.tv_nsec) -
                 (1000 * 1000 * 1000 * start.tv_sec + start.tv_nsec);
    total_repetitions += repetitions;
  }

  if (!checkfun(params, o)) {
    fprintf(
        stderr,
        "%-19s: %-30s @ lvl %3d: %8ld B -> %8ld B: CHECK FAILED!\n",
        params->run_name, bench_name, params->clevel,
        params->isize, o);
    raise(SIGABRT);
    return 0;
  }

  fprintf(
      stderr,
      "%-19s: %-30s @ lvl %3d: %8ld B -> %11.2lf B, %7ld iters, %10ld ns, %10ld ns/iter, %7.2lf MB/s\n",
      params->run_name, bench_name, params->clevel,
      params->isize, ((double)osize) / total_repetitions,
      total_repetitions, time_taken, time_taken / total_repetitions,
      ((double) 1000 * params->isize * total_repetitions) / time_taken
  );

  return time_taken;
}

#ifdef BENCH_ZSTD
ZSTD_CDict **create_zstd_cdicts(int min_level, int max_level, const char *dict_buf, size_t dict_size) {
  ZSTD_CDict **cdicts;
  int level;
  cdicts = malloc((max_level - min_level + 1) * sizeof(ZSTD_CDict *));
  CHECK(!cdicts, "malloc failed");

  cdicts -= min_level;

  for (level = min_level; level <= max_level; level++) {
    cdicts[level] = ZSTD_createCDict(dict_buf, dict_size, level);
    CHECK(!cdicts[level], "ZSTD_createCDict failed");
  }

  return cdicts;
}
#endif

int parse_args(args_t *a, int c, char *v[]) {
  int i;

  memset(a, 0, sizeof(args_t));

  a->prog_name = v[0];
  a->target_nanosec = BENCH_TARGET_NANOSEC;
  a->initial_reps = BENCH_INITIAL_REPETITIONS;
  a->starting_iter = BENCH_STARTING_ITER;

  for (i = 1; i < c; i++) {
    CHECK_R(v[i][0] != '-', "invalid argument");
    CHECK_R(v[i][2] != '\0', "cannot stack flags");
    switch(v[i][1]) {
    case 'h':
      a->print_help = 1;
      break;
    case 'i':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->in_fn = v[i];
      break;
    case 'D':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->dict_fn = v[i];
      break;
    case 'b':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->min_clevel = atoi(v[i]);
      break;
    case 'e':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->max_clevel = atoi(v[i]);
      break;
    case 'l':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->run_name = v[i];
      break;
    case 't': {
      char *end;
      i++;
      CHECK_R(i >= c, "missing argument");
      a->target_nanosec = strtoll(v[i], &end, 0);
      CHECK_R(end == v[i], "invalid argument");
      if (!strncmp(end, "", 1)) {
        // seconds by default
        a->target_nanosec *= 1000ull * 1000 * 1000;
      } else if (!strncmp(end, "m", 2)) {
        a->target_nanosec *= 60ull * 1000 * 1000 * 1000;
      } else if (!strncmp(end, "s", 2)) {
        a->target_nanosec *= 1000ull * 1000 * 1000;
      } else if (!strncmp(end, "ms", 3)) {
        a->target_nanosec *= 1000ull * 1000;
      } else if (!strncmp(end, "us", 3)) {
        a->target_nanosec *= 1000ull;
      } else if (!strncmp(end, "ns", 3)) {
      } else {
        CHECK_R(1, "invalid argument");
      }
    } break;
    case 'n':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->initial_reps = atoll(v[i]);
      break;
    case 's':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->starting_iter = atoll(v[i]);
      break;
    default:
      CHECK_R(1, "unrecognized flag");
    }
  }

  return 0;
}

void print_help(const args_t *a) {
  fprintf(stderr, "%s: compression benchmarking tool by Felix Handte\n", a->prog_name);
  fprintf(stderr, "\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "\t-h\tDisplay this help message\n");
  fprintf(stderr, "\t-i\tPath to input file (required)\n");
  fprintf(stderr, "\t-D\tPath to dictionary file\n");
  fprintf(stderr, "\t-b\tBeginning compression level (inclusive)\n");
  fprintf(stderr, "\t-e\tEnd compression level (inclusive)\n");
  fprintf(stderr, "\t-l\tLabel for run\n");
  fprintf(stderr, "\t-t\tTarget time to take benchmarking a param set (accepted suffixes: m, s (default), ms, us, ns) (default %lluns)\n", BENCH_TARGET_NANOSEC);
  fprintf(stderr, "\t-n\tInitial number of iterations for each call (default %llu)\n", BENCH_INITIAL_REPETITIONS);
  fprintf(stderr, "\t-s\tStarting iteration number (default %llu)\n", BENCH_STARTING_ITER);
}


int main(int argc, char *argv[]) {
  struct stat st;
  size_t bytes_read;

  size_t dict_size;
  char *dict_buf;
  FILE *dict_file;

  size_t in_size;
  size_t num_in_buf;
  size_t cur_in_buf;
  char *in_buf;
  FILE *in_file;

  size_t out_size = 0;
  char *out_buf;

  size_t check_size;
  char *check_buf;

#ifdef BENCH_LZ4
  LZ4_stream_t *ctx;
  LZ4_streamHC_t *hcctx;
  LZ4_stream_t *dictctx;
  LZ4_streamHC_t *dicthcctx;
  LZ4F_cctx *cctx;
  LZ4F_dctx *dctx;
  LZ4F_CDict *cdict;
  LZ4F_preferences_t prefs;
  LZ4F_compressOptions_t options;
#endif

#ifdef BENCH_ZSTD
  ZSTD_CCtx *zcctx;
  ZSTD_DCtx *zdctx;
  ZSTD_CDict **zcdicts;
  ZSTD_DDict *zddict;
#endif
  int clevel;

  bench_params_t params;

  args_t args;
  int parse_success;
  parse_success = parse_args(&args, argc, argv);
  CHECK(parse_success, "failed to parse args");
  if (args.print_help) {
    print_help(&args);
    return 0;
  }

  if (args.dict_fn != NULL) {
    CHECK(stat(args.dict_fn, &st), "stat(%s) failed", args.dict_fn);
    dict_size = st.st_size;
    dict_buf = (char *)malloc(dict_size);
    CHECK(!dict_buf, "malloc failed");
    dict_file = fopen(args.dict_fn, "r");
    bytes_read = fread(dict_buf, 1, dict_size, dict_file);
    CHECK(bytes_read != dict_size, "unexpected dict size");
  } else {
    dict_buf = "";
    dict_size = 0;
  }

  CHECK(stat(args.in_fn, &st), "stat(%s) failed", args.in_fn);
  in_size = st.st_size;
  num_in_buf = 256 * 1024 * 1024 / in_size;
  if (num_in_buf == 0) {
    num_in_buf = 1;
  }

  in_buf = (char *)malloc(in_size * num_in_buf);
  CHECK(!in_buf, "malloc failed");
  in_file = fopen(args.in_fn, "r");
  bytes_read = fread(in_buf, 1, in_size, in_file);
  CHECK(bytes_read != in_size, "unexpected input size");

  for(cur_in_buf = 1; cur_in_buf < num_in_buf; cur_in_buf++) {
    memcpy(in_buf + cur_in_buf * in_size, in_buf, in_size);
  }

  check_size = in_size;
  check_buf = (char *)malloc(check_size);
  CHECK(!check_buf, "malloc failed");

#ifdef BENCH_LZ4
  memset(&prefs, 0, sizeof(prefs));
  prefs.autoFlush = 1;
  if (in_size < 64 * 1024)
    prefs.frameInfo.blockMode = LZ4F_blockIndependent;
  prefs.frameInfo.contentSize = in_size;

  memset(&options, 0, sizeof(options));
  options.stableSrc = 1;

  LZ4F_CHECK_R(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION), 1);
  CHECK(!cctx, "LZ4F_createCompressionContext failed");

  LZ4F_CHECK_R(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION), 1);
  CHECK(!dctx, "LZ4F_createDecompressionContext failed");

  ctx = LZ4_createStream();
  CHECK(!ctx, "LZ4_createStream failed");

  hcctx = LZ4_createStreamHC();
  CHECK(!hcctx, "LZ4_createStreamHC failed");

  dictctx = LZ4_createStream();
  CHECK(!ctx, "LZ4_createStream failed");
  LZ4_loadDict(dictctx, dict_buf, dict_size);

  dicthcctx = LZ4_createStreamHC();
  CHECK(!hcctx, "LZ4_createStreamHC failed");
  LZ4_loadDictHC(dicthcctx, dict_buf, dict_size);

  cdict = LZ4F_createCDict(dict_buf, dict_size);
  CHECK(!cdict, "LZ4F_createCDict failed");
#endif

#ifdef BENCH_ZSTD
  zcctx = ZSTD_createCCtx();
  CHECK(!zcctx, "ZSTD_createCCtx failed");

  zdctx = ZSTD_createDCtx();
  CHECK(!zdctx, "ZSTD_createDCtx failed");

  if (args.dict_fn) {
    zcdicts = create_zstd_cdicts(args.min_clevel, args.max_clevel, dict_buf, dict_size);
    CHECK(!zcdicts, "create_zstd_cdicts failed");

    zddict = ZSTD_createDDict(dict_buf, dict_size);
    CHECK(!zddict, "ZSTD_createDDict failed");
  } else {
    zcdicts = NULL;
    zddict = NULL;
  }
#endif

#ifdef BENCH_LZ4
  if ((size_t)LZ4_compressBound(in_size) > out_size) {
    out_size = LZ4_compressBound(in_size);
  }
  if (LZ4F_compressFrameBound(in_size, &prefs) > out_size) {
    out_size = LZ4F_compressFrameBound(in_size, &prefs);
  }
#endif
#ifdef BENCH_ZSTD
  if (ZSTD_compressBound(in_size) > out_size) {
    out_size = ZSTD_compressBound(in_size);
  }
#endif
  out_buf = (char *)malloc(out_size);
  CHECK(!out_buf, "malloc failed");

  params.run_name = args.run_name;
#ifdef BENCH_LZ4
  params.ctx = ctx;
  params.hcctx = hcctx;
  params.dictctx = dictctx;
  params.dicthcctx = dicthcctx;
  params.cctx = cctx;
  params.dctx = dctx;
  params.cdict = cdict;
  params.prefs = &prefs;
  params.options = &options;
#endif
#ifdef BENCH_ZSTD
  params.zcctx = zcctx;
  params.zdctx = zdctx;
  params.zcdicts = zcdicts;
  params.zddict = zddict;
#endif
  params.dictbuf = dict_buf;
  params.dictsize = dict_size;
  params.obuf = out_buf;
  params.osize = out_size;
  params.ibuf = in_buf;
  params.isize = in_size;
  params.num_ibuf = num_in_buf;
  params.checkbuf = check_buf;
  params.checksize = check_size;
  params.clevel = 1;

#ifdef BENCH_LZ4
  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    bench("LZ4_compress_default"         , compress_default    , check_lz4 , &params, &args);
  }

  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    bench("LZ4_compress_fast_extState"   , compress_extState   , check_lz4 , &params, &args);
  }

  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    bench("LZ4_compress_HC"              , compress_hc         , check_lz4 , &params, &args);
  }

  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    bench("LZ4_compress_HC_extStateHC"   , compress_hc_extState, check_lz4 , &params, &args);
  }

  if (args.dict_fn) {
    for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
      params.clevel = clevel;
      bench("LZ4_compress_attach_dict"    , compress_dict        , check_lz4 , &params, &args);
    }

    for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
      params.clevel = clevel;
      bench("LZ4_compress_HC_attach_dict" , compress_hc_dict     , check_lz4 , &params, &args);
    }
  }

  params.cdict = NULL;
  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    params.prefs->compressionLevel = clevel;
    bench("LZ4F_compressFrame"           , compress_frame      , check_lz4f, &params, &args);
  }

  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    params.prefs->compressionLevel = clevel;
    bench("LZ4F_compressBegin"           , compress_begin      , check_lz4f, &params, &args);
  }
  
  if (args.dict_fn) {
    params.cdict = cdict;
    for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
      params.clevel = clevel;
      params.prefs->compressionLevel = clevel;
      bench("LZ4F_compressFrame_usingCDict", compress_frame      , check_lz4f, &params, &args);
    }

    for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
      params.clevel = clevel;
      params.prefs->compressionLevel = clevel;
      bench("LZ4F_compressBegin_usingCDict", compress_begin      , check_lz4f, &params, &args);
    }
  }
#endif

#ifdef BENCH_ZSTD
  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    bench("ZSTD_compress"                , zstd_compress_default, check_zstd, &params, &args);
  }

  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    bench("ZSTD_compressCCtx"            , zstd_compress_cctx   , check_zstd, &params, &args);
  }

  if (args.dict_fn) {
    for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
      params.clevel = clevel;
      bench("ZSTD_compress_usingCDict"     , zstd_compress_cdict  , check_zstd, &params, &args);
    }
  }
#endif

  return 0;
}
