#include <assert.h>
#include <dirent.h>
#include <errno.h>
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
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#endif

#ifdef BENCH_BROTLI
#include "brotli/decode.h"
#include "brotli/encode.h"
#endif

#ifdef BENCH_ZLIB
#include <zlib.h>
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

#ifndef BENCH_DEFAULT_NUM_CONTEXTS
#define BENCH_DEFAULT_NUM_CONTEXTS 1ull
#endif

#ifndef BENCH_DEFAULT_NUM_DICTS
#define BENCH_DEFAULT_NUM_DICTS 1ull
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
  size_t max_input_size;
  size_t target_nanosec;
  size_t initial_reps;
  size_t outer_reps;
  size_t starting_iter;
  size_t num_contexts;
  size_t num_dicts;
} args_t;

typedef struct {
  char *buf;
  size_t size;
  const char *fn;
} input_t;

typedef struct {
  const char *run_name;
  size_t iter;
  size_t ncctx;
  size_t ndctx;
  size_t ndicts;
  size_t curcctx;
  size_t curdctx;
  size_t curdict;
#ifdef BENCH_LZ4
  LZ4_stream_t **ctx;
  LZ4_streamHC_t **hcctx;
  LZ4_stream_t **dictctx;
  LZ4_streamHC_t **dicthcctx;
  LZ4F_cctx **cctx;
  LZ4F_dctx **dctx;
  const LZ4F_CDict* cdict;
  LZ4F_preferences_t* prefs;
  const LZ4F_compressOptions_t* options;
#endif
#ifdef BENCH_ZSTD
  ZSTD_CCtx **zcctx;
  ZSTD_DCtx **zdctx;
  ZSTD_CDict ***zcdicts;
  ZSTD_DDict *zddict;
#endif
#ifdef BENCH_BROTLI
  BrotliEncoderState *brcctx;
  BrotliDecoderState *brdctx;
#endif
#ifdef BENCH_ZLIB
  z_stream *gzctx;
#endif
  const char *dictbuf;
  size_t dictsize;
  char *obuf;
  size_t osize;
  const char* isample;
  size_t isize;
  const char* ifn;

  const input_t *inputs;
  size_t num_inputs;
  size_t max_input_size;

  char *checkbuf;
  size_t checksize;
  int clevel;
} bench_params_t;

#ifdef BENCH_LZ4
size_t compress_frame(bench_params_t *p) {
#ifdef BENCH_LZ4_COMPRESSFRAME_USINGCDICT_TAKES_CCTX
  LZ4F_cctx *cctx = p->cctx[p->curcctx];
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
  LZ4F_cctx *cctx = p->cctx[p->curcctx];
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
  LZ4_stream_t *ctx = p->ctx[p->curcctx];
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
  LZ4_streamHC_t *hcctx = p->hcctx[p->curcctx];
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
  LZ4_stream_t *ctx = p->ctx[p->curcctx];
  const LZ4_stream_t *dictctx = p->dictctx[p->curcctx];
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
  LZ4_streamHC_t *hcctx = p->hcctx[p->curcctx];
  const LZ4_streamHC_t *dicthcctx = p->dicthcctx[p->curcctx];
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
  ZSTD_CCtx *ctx = p->zcctx[p->curcctx];
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  size_t oused;

  oused = ZSTD_compressCCtx(ctx, obuf, osize, isample, isize, clevel);

  return oused;
}

size_t zstd_compress_stream(bench_params_t *p) {
  ZSTD_CCtx *ctx = p->zcctx[p->curcctx];
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  ZSTD_outBuffer obuffer = {obuf, osize, 0};
  ZSTD_inBuffer ibuffer = {isample, isize, 0};

  size_t ret;

  ZSTD_CCtx_reset(ctx, ZSTD_reset_session_and_parameters);
  ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, clevel);

  ret = ZSTD_compressStream2(ctx, &obuffer, &ibuffer, ZSTD_e_flush);

  if (ZSTD_isError(ret)) {
    return 0;
  }

  ret = ZSTD_compressStream2(ctx, &obuffer, &ibuffer, ZSTD_e_end);

  if (ret) {
    return 0;
  }

  return obuffer.pos;
}

size_t zstd_compress_cdict(bench_params_t *p) {
  ZSTD_CCtx *ctx = p->zcctx[p->curcctx];
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;
  ZSTD_CDict *cdict = p->zcdicts[clevel][p->curdict];

  size_t oused;

  ZSTD_CCtx_setParameter(ctx, ZSTD_c_forceAttachDict, ZSTD_dictForceAttach);

  oused = ZSTD_compress_usingCDict(ctx, obuf, osize, isample, isize, cdict);

  return oused;
}

size_t zstd_compress_stream_cdict(bench_params_t *p) {
  ZSTD_CCtx *ctx = p->zcctx[p->curcctx];
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;
  ZSTD_CDict *cdict = p->zcdicts[clevel][p->curdict];

  ZSTD_outBuffer obuffer = {obuf, osize, 0};
  ZSTD_inBuffer ibuffer = {isample, isize, 0};

  size_t ret;

  ZSTD_CCtx_reset(ctx, ZSTD_reset_session_and_parameters);
  ZSTD_CCtx_refCDict(ctx, cdict);
  ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, clevel);

  ret = ZSTD_compressStream2(ctx, &obuffer, &ibuffer, ZSTD_e_flush);

  if (ZSTD_isError(ret)) {
    return 0;
  }

  ret = ZSTD_compressStream2(ctx, &obuffer, &ibuffer, ZSTD_e_end);

  if (ret) {
    return 0;
  }

  return obuffer.pos;
}

size_t zstd_setup_compress_cdict_split_params(bench_params_t *p) {
  int clevel = p->clevel;
  ZSTD_CCtx *zcctx = p->zcctx[p->curcctx];
  ZSTD_CDict *zcdict = p->zcdicts[clevel][p->curdict];
  ZSTD_CCtx_reset(zcctx, ZSTD_reset_session_and_parameters);
  ZSTD_CCtx_refCDict(zcctx, zcdict);
  ZSTD_CCtx_setParameter(zcctx, ZSTD_c_compressionLevel, clevel);
  ZSTD_CCtx_setParameter(zcctx, ZSTD_c_hashLog, 12);
  ZSTD_CCtx_setParameter(zcctx, ZSTD_c_chainLog, 12);
  ZSTD_CCtx_setParameter(zcctx, ZSTD_c_forceAttachDict, 1);
  return 1;
}

size_t zstd_compress_cdict_split_params(bench_params_t *p) {
  ZSTD_CCtx *ctx = p->zcctx[p->curcctx];
  char *obuf = p->obuf;
  size_t osize = p->osize;
  size_t opos = 0;
  const char* isample = p->isample;
  size_t isize = p->isize;
  size_t ipos = 0;

  size_t oused;

  oused = ZSTD_compressStream2_simpleArgs(ctx, obuf, osize, &opos, isample, isize, &ipos, ZSTD_e_end);

  if (ZSTD_isError(oused)) return 0;

  return opos;
}
#endif

#ifdef BENCH_BROTLI
size_t brotli_compress(bench_params_t *p) {
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  size_t oused = osize;
  BROTLI_BOOL ret;

  ret = BrotliEncoderCompress(
    clevel, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE,
    isize, isample,
    &oused, obuf);

  if (ret == BROTLI_FALSE) {
    fprintf(stderr, "Sad!\n");
    return 0;
  }

  return oused;
}
#endif

#ifdef BENCH_ZLIB
size_t compress_gz(bench_params_t* p) {
  z_stream *gzctx = &p->gzctx[p->curcctx];
  char *obuf = p->obuf;
  size_t osize = p->osize;
  const char* isample = p->isample;
  size_t isize = p->isize;
  int clevel = p->clevel;

  size_t oused;

  if (deflateInit(gzctx, clevel) != Z_OK) {
    return 0;
  }

  gzctx->next_in = isample;
  gzctx->avail_in = isize;
  gzctx->next_out = obuf;
  gzctx->avail_out = osize;

  if (deflate(gzctx, Z_FINISH) != Z_STREAM_END) {
    return 0;
  }

  oused = gzctx->total_out;

  if (deflateEnd(gzctx) != Z_OK) {
    return 0;
  }

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
  LZ4F_dctx *dctx = p->dctx[p->curdctx];
  memset(p->checkbuf, 0xFF, p->checksize);
  LZ4F_resetDecompressionContext(dctx);
  do {
    ret = LZ4F_decompress_usingDict(
        dctx, p->checkbuf + dp, &dleft, p->obuf + cp, &cleft,
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
  return ZSTD_decompress_usingDDict(p->zdctx[p->curdctx], p->checkbuf, p->checksize, p->obuf, csize, p->zddict) == p->isize
      && !memcmp(p->isample, p->checkbuf, p->isize);
}
#endif

#ifdef BENCH_BROTLI
size_t check_brotli(bench_params_t *p, size_t csize) {
  (void)csize;
  (void)p;
  // memset(p->checkbuf, 0xFF, p->checksize);
  // return ZSTD_decompress_usingDDict(p->zdctx, p->checkbuf, p->checksize, p->obuf, csize, p->zddict) == p->isize
  //     && !memcmp(p->isample, p->checkbuf, p->isize);
  return 1;
}
#endif

#ifdef BENCH_ZLIB
size_t check_gz(bench_params_t *p, size_t csize) {
  (void)csize;
  (void)p;
  // TODO
  return 1;
}
#endif

uint64_t bench(
    const char *bench_name,
    size_t (*setup)(bench_params_t *),
    size_t (*fun)(bench_params_t *),
    size_t (*checkfun)(bench_params_t *, size_t),
    bench_params_t *params,
    const args_t *args
) {
  struct timespec start, end;
  size_t i, osize = 0, o = 0;
  size_t time_taken = 0;
  uint64_t total_repetitions = 0;
  uint64_t total_input_size = 0;
  uint64_t repetitions = args->initial_reps;
  int clevel = params->clevel;

  if (setup) {
    for (i = 0; i < params->ncctx || i < params->ndctx; i++) {
      params->curcctx = i % params->ncctx;
      params->curdctx = i % params->ndctx;
      params->curdict = i % params->ndicts;
      if (!setup(params)) {
        return 0;
      }
    }
  }

  if (clock_gettime(CLOCK_MONOTONIC_RAW, &start)) return 0;

  while (total_repetitions == 0 || time_taken < args->target_nanosec) {
    if (total_repetitions) {
        repetitions = total_repetitions; // double previous
    }

    for (i = args->starting_iter; i < args->starting_iter + repetitions; i++) {
      // params->clevel = clevel + (i & 1);
      params->iter = i;
      params->curcctx = i % params->ncctx;
      params->curdctx = i % params->ndctx;
      params->curdict = i % params->ndicts;
      params->isample = params->inputs[i % params->num_inputs].buf;
      params->isize = params->inputs[i % params->num_inputs].size;
      params->ifn = params->inputs[i % params->num_inputs].fn;
      if (args->max_input_size && params->isize > args->max_input_size) {
        params->isize = args->max_input_size;
      }
      total_input_size += params->isize;
//       if (params->num_inputs == 1) {
//       } else {
// #ifdef BENCH_RANDOMIZE_INPUT
//         params->isample = params->ibuf + ((i * 2654435761U) % ((params->num_ibuf - 1) * params->isize));
// #else
//         params->isample = params->ibuf + ((i * 2654435761U) % params->num_ibuf) * params->isize;
// #endif
//       }
      o = fun(params);
      if (!o) {
        fprintf(
            stderr,
            "%-19s: %-30s @ lvl %3d, %3zd ctxs: %8ld B: FAILED!\n",
            params->run_name, bench_name, params->clevel, params->ncctx,
            params->isize);
        return 0;
      }
      // fprintf(
      //     stderr,
      //     "%-19s: %-30s @ lvl %3d, %3zd ctxs: %8ld B -> %8ld B: iter %8ld, ifn %s\n",
      //     params->run_name, bench_name, params->clevel, params->ncctx,
      //     params->isize, o, i, params->ifn);
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
        "%-19s: %-30s @ lvl %3d, %3zd ctxs: %8ld B -> %8ld B: CHECK FAILED!\n",
        params->run_name, bench_name, params->clevel, params->ncctx,
        params->isize, o);
    raise(SIGABRT);
    return 0;
  }

  fprintf(
      stderr,
      "%-19s: %-30s @ lvl %3d, %3zd ctxs: %8ld B -> %11.2lf B, %7ld iters, %10ld ns, %10ld ns/iter, %7.2lf MB/s\n",
      params->run_name, bench_name, params->clevel, params->ncctx,
      total_input_size / total_repetitions,
      ((double)osize) / total_repetitions,
      total_repetitions, time_taken, time_taken / total_repetitions,
      ((double) 1000 * total_input_size) / time_taken
  );

  params->clevel = clevel;

  return time_taken;
}

#ifdef BENCH_ZSTD
ZSTD_CDict ***create_zstd_cdicts(int min_level, int max_level, int ndicts, const char *dict_buf, size_t dict_size) {
  ZSTD_CDict ***cdicts;
  int level;
  int dictnum;
  if (max_level < 22) max_level = 22;
  cdicts = malloc((max_level - min_level + 1) * sizeof(ZSTD_CDict **));
  CHECK(!cdicts, "malloc failed");

  cdicts -= min_level;

  for (level = min_level; level <= max_level; level++) {
    cdicts[level] = malloc(ndicts * sizeof(ZSTD_CDict *));
    CHECK(!cdicts[level], "malloc failed");
    for (dictnum = 0; dictnum < ndicts; dictnum++) {
#ifdef ZSTD_c_enableDedicatedDictSearch
      ZSTD_CCtx_params* cctx_params = ZSTD_createCCtxParams();
      ZSTD_CCtxParams_init(cctx_params, level);
      ZSTD_CCtxParams_setParameter(cctx_params, ZSTD_c_enableDedicatedDictSearch, 1);

      ZSTD_CCtxParams_setParameter(cctx_params, ZSTD_c_compressionLevel, level);
      cdicts[level][dictnum] = ZSTD_createCDict_advanced2(
        dict_buf,
        dict_size,
        ZSTD_dlm_byCopy,
        ZSTD_dct_auto,
        cctx_params,
        ZSTD_defaultCMem);
      ZSTD_freeCCtxParams(cctx_params);
#else
      ZSTD_compressionParameters cparams = ZSTD_getCParams(level, ZSTD_CONTENTSIZE_UNKNOWN, dict_size);
      cdicts[level][dictnum] = ZSTD_createCDict_advanced(
        dict_buf,
        dict_size,
        ZSTD_dlm_byCopy,
        ZSTD_dct_auto,
        cparams,
        ZSTD_defaultCMem);
#endif
      CHECK(!cdicts[level][dictnum], "ZSTD_createCDict failed");
    }
  }

  return cdicts;
}
#endif


int read_input(const char *in_fn, input_t *i) {
  size_t in_size;
  size_t num_in_buf;
  // size_t cur_in_buf;
  size_t bytes_read;
  char *in_buf;
  FILE *in_file;
  struct stat st;

  CHECK_R(stat(in_fn, &st), "stat(%s) failed: %m", in_fn);
  in_size = st.st_size;
  // num_in_buf = 256 * 1024 * 1024 / in_size;
  // if (num_in_buf == 0) {
    num_in_buf = 1;
  // }

  in_buf = (char *)malloc(in_size * num_in_buf);
  CHECK_R(!in_buf, "malloc failed");
  in_file = fopen(in_fn, "r");
  bytes_read = fread(in_buf, 1, in_size, in_file);
  CHECK_R(bytes_read != in_size, "unexpected input size");

  // for(cur_in_buf = 1; cur_in_buf < num_in_buf; cur_in_buf++) {
  //   memcpy(in_buf + cur_in_buf * in_size, in_buf, in_size);
  // }

  i->buf = in_buf;
  i->size = in_size;
  i->fn = in_fn;

  CHECK_R(fclose(in_file), "fclose(%s) failed: %m", in_fn);

  return 0;
}

int read_inputs(args_t *a, bench_params_t *p) {
  struct stat st;
  size_t max_input_size = 0;
  size_t a_ins = 1;
  size_t n_ins = 0;
  input_t *ins = malloc(a_ins * sizeof(input_t));
  CHECK_R(!ins, "malloc failed");
  CHECK_R(stat(a->in_fn, &st), "stat(%s) failed", a->in_fn);
  if ((st.st_mode & S_IFMT) == S_IFDIR) {
    // it's a directory, accumulate all files inside as inputs
    DIR *d = opendir(a->in_fn);
    struct dirent *de;
    CHECK_R(!d, "opendir() failed: %m");

    errno = 0;
    while ((de = readdir(d))) {
      char *in_fn;
      size_t fn_path_end;
      if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
        continue;
      }

      in_fn = malloc(strlen(a->in_fn) + 1 /* slash */ + NAME_MAX + 1 /* nul */);
      fn_path_end = strlen(a->in_fn);

      CHECK_R(!in_fn, "malloc failed");
      strcpy(in_fn, a->in_fn);
      if (in_fn[fn_path_end - 1] != '/') {
        in_fn[fn_path_end++] = '/';
      }
      strcpy(in_fn + fn_path_end, de->d_name);

      if (n_ins == a_ins) {
        a_ins *= 2;
        ins = realloc(ins, a_ins * sizeof(input_t));
        CHECK_R(!ins, "realloc failed");
      }

      CHECK_R(read_input(in_fn, ins + n_ins), "read_input(%s) failed", in_fn);
      if (ins[n_ins].size > max_input_size) {
        max_input_size = ins[n_ins].size;
      }
      n_ins++;
    }
    CHECK_R(errno, "readdir() failed: %m");

    CHECK_R(closedir(d), "closedir() failed: %m");
  } else {
    // it's a file, use as input
    CHECK_R(read_input(a->in_fn, ins), "read_input() failed");
    max_input_size = ins[0].size;
    n_ins++;
  }

  p->inputs = ins;
  p->num_inputs = n_ins;
  p->max_input_size = max_input_size;

  return 0;
}

int parse_args(args_t *a, int c, char *v[]) {
  int i;

  memset(a, 0, sizeof(args_t));

  a->prog_name = v[0];
  a->target_nanosec = BENCH_TARGET_NANOSEC;
  a->initial_reps = BENCH_INITIAL_REPETITIONS;
  a->starting_iter = BENCH_STARTING_ITER;
  a->num_contexts = BENCH_DEFAULT_NUM_CONTEXTS;
  a->num_dicts = BENCH_DEFAULT_NUM_DICTS;
  a->outer_reps = 1;

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
    case 'R':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->outer_reps = atoll(v[i]);
      break;
    case 's':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->starting_iter = atoll(v[i]);
      break;
    case 'S':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->max_input_size = atoll(v[i]);
      break;
    case 'c':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->num_contexts = atoll(v[i]);
      break;
    case 'd':
      i++;
      CHECK_R(i >= c, "missing argument");
      a->num_dicts = atoll(v[i]);
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
  fprintf(stderr, "\t-R\tNumber of times to re-run the same benchmark\n");
  fprintf(stderr, "\t-s\tStarting iteration number (default %llu)\n", BENCH_STARTING_ITER);
  fprintf(stderr, "\t-c\tNumber of (de)compression contexts to rotate through using (default %llu)\n", BENCH_DEFAULT_NUM_CONTEXTS);
  fprintf(stderr, "\t-d\tNumber of materialized dictionaries to rotate through using (default %llu)\n", BENCH_DEFAULT_NUM_CONTEXTS);
}


int main(int argc, char *argv[]) {
  size_t i;

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
  ZSTD_CDict ***zcdicts;
  ZSTD_DDict *zddict;
#endif

#ifdef BENCH_BROTLI
  BrotliEncoderState *brcctx;
  BrotliDecoderState *brdctx;
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
    input_t dict_input;
    CHECK_R(read_input(args.dict_fn, &dict_input), "read_input(%s) failed", args.dict_fn);
    params.dictbuf = dict_input.buf;
    params.dictsize = dict_input.size;
  } else {
    params.dictbuf = "";
    params.dictsize = 0;
  }

  CHECK(read_inputs(&args, &params), "read_inputs() failed");

  check_size = params.max_input_size;
  check_buf = (char *)malloc(check_size);
  CHECK(!check_buf, "malloc failed");
  params.checkbuf = check_buf;
  params.checksize = check_size;

  params.ncctx = args.num_contexts;
  params.ndctx = args.num_contexts;
  params.ndicts = args.num_dicts;

#ifdef BENCH_LZ4
  memset(&prefs, 0, sizeof(prefs));
  prefs.autoFlush = 1;
  // if (in_size < 64 * 1024)
  //   prefs.frameInfo.blockMode = LZ4F_blockIndependent;
  // prefs.frameInfo.contentSize = in_size;

  memset(&options, 0, sizeof(options));
  options.stableSrc = 1;

  params.ctx = malloc(args.num_contexts * sizeof(LZ4_stream_t *));
  params.hcctx = malloc(args.num_contexts * sizeof(LZ4_streamHC_t *));
  params.dictctx = malloc(args.num_contexts * sizeof(LZ4_stream_t *));
  params.dicthcctx = malloc(args.num_contexts * sizeof(LZ4_streamHC_t *));
  params.cctx = malloc(args.num_contexts * sizeof(LZ4F_cctx *));
  params.dctx = malloc(args.num_contexts * sizeof(LZ4F_dctx *));
  CHECK(!params.ctx, "malloc failed");
  CHECK(!params.hcctx, "malloc failed");
  CHECK(!params.dictctx, "malloc failed");
  CHECK(!params.dicthcctx, "malloc failed");
  CHECK(!params.cctx, "malloc failed");
  CHECK(!params.dctx, "malloc failed");

  for (i = 0; i < args.num_contexts; i++) {
    LZ4F_CHECK_R(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION), 1);
    CHECK(!cctx, "LZ4F_createCompressionContext failed");
    params.cctx[i] = cctx;

    LZ4F_CHECK_R(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION), 1);
    CHECK(!dctx, "LZ4F_createDecompressionContext failed");
    params.dctx[i] = dctx;

    ctx = LZ4_createStream();
    CHECK(!ctx, "LZ4_createStream failed");
    params.ctx[i] = ctx;

    hcctx = LZ4_createStreamHC();
    CHECK(!hcctx, "LZ4_createStreamHC failed");
    params.hcctx[i] = hcctx;

    dictctx = LZ4_createStream();
    CHECK(!ctx, "LZ4_createStream failed");
    LZ4_loadDict(dictctx, params.dictbuf, params.dictsize);
    params.dictctx[i] = dictctx;

    dicthcctx = LZ4_createStreamHC();
    CHECK(!hcctx, "LZ4_createStreamHC failed");
    LZ4_loadDictHC(dicthcctx, params.dictbuf, params.dictsize);
    params.dicthcctx[i] = dicthcctx;
  }

  cdict = LZ4F_createCDict(params.dictbuf, params.dictsize);
  CHECK(!cdict, "LZ4F_createCDict failed");
#endif

#ifdef BENCH_ZSTD
  params.zcctx = malloc(args.num_contexts * sizeof(ZSTD_CCtx *));
  params.zdctx = malloc(args.num_contexts * sizeof(ZSTD_DCtx *));
  CHECK(!params.zcctx, "malloc failed");
  CHECK(!params.zdctx, "malloc failed");

  for (i = 0; i < args.num_contexts; i++) {
    zcctx = ZSTD_createCCtx();
    CHECK(!zcctx, "ZSTD_createCCtx failed");
    params.zcctx[i] = zcctx;

    zdctx = ZSTD_createDCtx();
    CHECK(!zdctx, "ZSTD_createDCtx failed");
    params.zdctx[i] = zdctx;
  }

  if (args.dict_fn) {
    zcdicts = create_zstd_cdicts(args.min_clevel, args.max_clevel, args.num_dicts, params.dictbuf, params.dictsize);
    CHECK(!zcdicts, "create_zstd_cdicts failed");

    zddict = ZSTD_createDDict(params.dictbuf, params.dictsize);
    CHECK(!zddict, "ZSTD_createDDict failed");
  } else {
    zcdicts = NULL;
    zddict = NULL;
  }
#endif

#ifdef BENCH_BROTLI
  brcctx = BrotliEncoderCreateInstance(NULL, NULL, NULL);
  CHECK(!brcctx, "BrotliEncoderCreateInstance failed");

  brdctx = BrotliDecoderCreateInstance(NULL, NULL, NULL);
  CHECK(!brdctx, "BrotliDecoderCreateInstance failed");
#endif

#ifdef BENCH_ZLIB
  params.gzctx = malloc(args.num_contexts * sizeof(z_stream));
  CHECK(!params.gzctx, "Creating zlib ctxes failed");
  for (i = 0; i < args.num_contexts; i++) {
    memset(&params.gzctx[i], 0, sizeof(z_stream));
    params.gzctx[i].zalloc = Z_NULL;
    params.gzctx[i].zfree = Z_NULL;
    params.gzctx[i].opaque = Z_NULL;
  }
#endif

#ifdef BENCH_LZ4
  if ((size_t)LZ4_compressBound(params.max_input_size) > out_size) {
    out_size = LZ4_compressBound(params.max_input_size);
  }
  if (LZ4F_compressFrameBound(params.max_input_size, &prefs) > out_size) {
    out_size = LZ4F_compressFrameBound(params.max_input_size, &prefs);
  }
#endif
#ifdef BENCH_ZSTD
  if (ZSTD_compressBound(params.max_input_size) > out_size) {
    out_size = ZSTD_compressBound(params.max_input_size);
  }
#endif
#ifdef BENCH_BROTLI
  if (BrotliEncoderMaxCompressedSize(params.max_input_size) > out_size) {
    out_size = BrotliEncoderMaxCompressedSize(params.max_input_size);
  }
#endif
#ifdef BENCH_ZLIB
  if (compressBound(params.max_input_size) > out_size) {
    out_size = compressBound(params.max_input_size);
  }
#endif
  out_buf = (char *)malloc(out_size);
  CHECK(!out_buf, "malloc failed");

  params.run_name = args.run_name;
#ifdef BENCH_LZ4
  params.cdict = cdict;
  params.prefs = &prefs;
  params.options = &options;
#endif
#ifdef BENCH_ZSTD
  params.zcdicts = zcdicts;
  params.zddict = zddict;
#endif
  params.obuf = out_buf;
  params.osize = out_size;
  params.clevel = 1;

#ifdef BENCH_LZ4
  // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //   params.clevel = clevel;
  //   for (i = 0; i < args.outer_reps; i++)
  //   bench("LZ4_compress_default"         , NULL, compress_default    , check_lz4 , &params, &args);
  // }

  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    params.clevel = clevel;
    for (i = 0; i < args.outer_reps; i++)
    bench("LZ4_compress_fast_extState"   , NULL, compress_extState   , check_lz4 , &params, &args);
  }

  // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //   params.clevel = clevel;
  //   for (i = 0; i < args.outer_reps; i++)
  //   bench("LZ4_compress_HC"              , NULL, compress_hc         , check_lz4 , &params, &args);
  // }

  // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //   params.clevel = clevel;
  //   for (i = 0; i < args.outer_reps; i++)
  //   bench("LZ4_compress_HC_extStateHC"   , NULL, compress_hc_extState, check_lz4 , &params, &args);
  // }

  if (args.dict_fn) {
    for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
      params.clevel = clevel;
      for (i = 0; i < args.outer_reps; i++)
      bench("LZ4_compress_attach_dict"    , NULL, compress_dict        , check_lz4 , &params, &args);
    }

    // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    //   params.clevel = clevel;
    //   for (i = 0; i < args.outer_reps; i++)
    //   bench("LZ4_compress_HC_attach_dict" , NULL, compress_hc_dict     , check_lz4 , &params, &args);
    // }
  }

  // params.cdict = NULL;
  // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //   params.clevel = clevel;
  //   params.prefs->compressionLevel = clevel;
  //   for (i = 0; i < args.outer_reps; i++)
  //   bench("LZ4F_compressFrame"           , NULL, compress_frame      , check_lz4f, &params, &args);
  // }

  // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //   params.clevel = clevel;
  //   params.prefs->compressionLevel = clevel;
  //   for (i = 0; i < args.outer_reps; i++)
  //   bench("LZ4F_compressBegin"           , NULL, compress_begin      , check_lz4f, &params, &args);
  // }

  // if (args.dict_fn) {
  //   params.cdict = cdict;
  //   for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //     params.clevel = clevel;
  //     params.prefs->compressionLevel = clevel;
  //     for (i = 0; i < args.outer_reps; i++)
  //     bench("LZ4F_compressFrame_usingCDict", NULL, compress_frame      , check_lz4f, &params, &args);
  //   }

  //   for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //     params.clevel = clevel;
  //     params.prefs->compressionLevel = clevel;
  //     for (i = 0; i < args.outer_reps; i++)
  //     bench("LZ4F_compressBegin_usingCDict", NULL, compress_begin      , check_lz4f, &params, &args);
  //   }
  // }
#endif

#ifdef BENCH_ZSTD
  // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //   if (clevel > ZSTD_maxCLevel()) continue;
  //   if (clevel == 0) continue;
  //   params.clevel = clevel;
  //   for (i = 0; i < args.outer_reps; i++)
  //   bench("ZSTD_compress"                , NULL, zstd_compress_default, check_zstd, &params, &args);
  // }

  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    if (clevel > ZSTD_maxCLevel()) continue;
    if (clevel == 0) continue;
    params.clevel = clevel;
    for (i = 0; i < args.outer_reps; i++)
    bench("ZSTD_compressCCtx"            , NULL, zstd_compress_cctx   , check_zstd, &params, &args);
  }

  // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
  //   if (clevel > ZSTD_maxCLevel()) continue;
  //   if (clevel == 0) continue;
  //   params.clevel = clevel;
  //   for (i = 0; i < args.outer_reps; i++)
  //   bench("ZSTD_compress_stream"         , NULL, zstd_compress_stream , check_zstd, &params, &args);
  // }

  if (args.dict_fn) {
    for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
      if (clevel > ZSTD_maxCLevel()) continue;
      if (clevel == 0) continue;
      params.clevel = clevel;
      for (i = 0; i < args.outer_reps; i++)
      bench("ZSTD_compress_usingCDict"      , NULL, zstd_compress_cdict  , check_zstd, &params, &args);
    }

    // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    //   if (clevel > ZSTD_maxCLevel()) continue;
    //   if (clevel == 0) continue;
    //   params.clevel = clevel;
    //   for (i = 0; i < args.outer_reps; i++)
    //   bench("ZSTD_compress_stream_CDict", NULL, zstd_compress_stream_cdict, check_zstd, &params, &args);
    // }

    // for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    //   if (clevel == 0) continue;
    //   params.clevel = clevel;
    //   for (i = 0; i < args.outer_reps; i++)
    //   bench("ZSTD_compress_usingCDict_split",
    //         zstd_setup_compress_cdict_split_params,
    //         zstd_compress_cdict_split_params,
    //         check_zstd, &params, &args);
    // }
  }
#endif

#ifdef BENCH_BROTLI
  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    if (clevel < BROTLI_MIN_QUALITY) continue;
    if (clevel > BROTLI_MAX_QUALITY) continue;
    params.clevel = clevel;
    for (i = 0; i < args.outer_reps; i++)
    bench("BrotliEncoderCompress"          , NULL, brotli_compress        , check_brotli, &params, &args);
  }
#endif

#ifdef BENCH_ZLIB
  for (clevel = args.min_clevel; clevel <= args.max_clevel; clevel++) {
    if (clevel < Z_NO_COMPRESSION) continue;
    if (clevel > Z_BEST_COMPRESSION) continue;
    params.clevel = clevel;
    for (i = 0; i < args.outer_reps; i++)
    bench("compress_gz", NULL, compress_gz, check_gz, &params, &args);
  }
#endif

  return 0;
}
