
PROGDIR ?= /home/felixh/prog

LZ4DIR ?= $(PROGDIR)/lz4
LZ4LIBDIR ?= $(LZ4DIR)/lib
LZ4INCLUDES ?= -I$(LZ4DIR)/lib

LZ42DIR ?= $(PROGDIR)/lz4-2

ZSTDDIR ?= $(PROGDIR)/zstd
ZSTDLIBDIR ?= $(ZSTDDIR)/lib
ZSTDINCLUDES ?= -I$(ZSTDDIR)/lib -I$(ZSTDDIR)/lib/dictBuilder

ZSTD2DIR ?= $(PROGDIR)/zstd-2

BROTLIDIR ?= $(PROGDIR)/brotli
BROTLILIBDIR ?= $(BROTLIDIR)
BROTLIHDRDIR ?= $(BROTLIDIR)/c/include

ZSTDFLAGS = -DBENCH_ZSTD $(ZSTDINCLUDES)
LZ4FLAGS = -DBENCH_LZ4 $(LZ4INCLUDES)
BROTLIFLAGS = -DBENCH_BROTLI -I$(BROTLIHDRDIR)
ZLIBFLAGS = -DBENCH_ZLIB $(ZLIBINCLUDES)
BROTLILDFLAGS = -lm
ZLIBLDFLAGS = -lz

CFLAGS  ?= -O3 -DNDEBUG -march=native -mtune=native
DEBUGFLAGS:= -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow \
             -Wswitch-enum -Wdeclaration-after-statement -Wstrict-prototypes \
             -Wundef -Wpointer-arith -Wstrict-aliasing=1
CFLAGS  += $(DEBUGFLAGS) $(MOREFLAGS)
FLAGS    = $(CPPFLAGS) $(CFLAGS)

.PHONY: all
all: framebench

.PHONY: $(LZ4LIBDIR)/liblz4.a
$(LZ4LIBDIR)/liblz4.a:
	$(MAKE) -C $(LZ4LIBDIR) liblz4.a

.PHONY: $(ZSTDLIBDIR)/libzstd.a
$(ZSTDLIBDIR)/libzstd.a:
	$(MAKE) -C $(ZSTDLIBDIR) libzstd.a

.PHONY: $(BROTLILIBDIR)/libbrotli.a
$(BROTLILIBDIR)/libbrotli.a:
	$(MAKE) -C $(BROTLILIBDIR) lib

liblz4.a: $(LZ4LIBDIR)/liblz4.a
	cp $(LZ4LIBDIR)/liblz4.a liblz4.a

libzstd.a: $(ZSTDLIBDIR)/libzstd.a
	cp $(ZSTDLIBDIR)/libzstd.a libzstd.a

libbrotli.a: $(BROTLILIBDIR)/libbrotli.a
	cp $(BROTLILIBDIR)/libbrotli.a libbrotli.a


framebench: framebench.c liblz4.a libzstd.a
	$(CC) $(FLAGS) $(LZ4INCLUDES) $(ZSTDINCLUDES) -o framebench framebench.c liblz4.a libzstd.a $(LDFLAGS)

framebench-lz4: FLAGS+=$(LZ4FLAGS)
framebench-lz4: LDFLAGS+=$(LZ4LDFLAGS)
framebench-lz4: framebench.c liblz4.a
	$(CC) $(FLAGS) -o framebench-lz4 framebench.c liblz4.a $(LDFLAGS)

framebench-zstd: FLAGS+=$(ZSTDFLAGS)
framebench-zstd: LDFLAGS+=$(ZSTDLDFLAGS)
framebench-zstd: framebench.c libzstd.a
	$(CC) $(FLAGS) -o framebench-zstd framebench.c libzstd.a $(LDFLAGS)

framebench-brotli: FLAGS+=$(BROTLIFLAGS)
framebench-brotli: LDFLAGS+=$(BROTLILDFLAGS)
framebench-brotli: framebench.c libbrotli.a
	$(CC) $(FLAGS) -o framebench-brotli framebench.c libbrotli.a $(LDFLAGS)

.PHONY: zstdcompare
zstdcompare: framebench-zstd-dev framebench-zstd-exp

framebench-zstd-exp framebench-zstd-dev: MOREFLAGS?=-O3 -march=native -mtune=native -ggdb -DBENCH_TARGET_NANOSEC=250000000ull -DNDEBUG
# framebench-zstd-exp framebench-zstd-dev: MOREFLAGS+=-Og -ggdb -DDEBUGLEVEL=4 -DBENCH_TARGET_NANOSEC=250000000ull
framebench-zstd-exp framebench-zstd-dev: CC?=gcc-8
framebench-zstd-dev: MOREFLAGS+= -DFRAMEBENCH_DEV
framebench-zstd-exp: MOREFLAGS+= -DFRAMEBENCH_EXP

.PHONY: framebench-zstd-exp
framebench-zstd-exp:
	make clean
	#make clean-zstd ZSTDDIR=$(ZSTDDIR)
	make framebench-zstd -j32 ZSTDDIR=$(ZSTDDIR) CC="$(CC)" MOREFLAGS="$(MOREFLAGS)"
	mv framebench-zstd framebench-zstd-exp

.PHONY: framebench-zstd-dev
framebench-zstd-dev:
	make clean
	#make clean-zstd ZSTDDIR=$(ZSTD2DIR)
	make framebench-zstd -j32 ZSTDDIR=$(ZSTD2DIR) CC="$(CC)" MOREFLAGS="$(MOREFLAGS)"
	mv framebench-zstd framebench-zstd-dev

framebench-all: MOREFLAGS?=-O3 -march=native -mtune=native -ggdb -DBENCH_TARGET_NANOSEC=250000000ull -DNDEBUG -DBENCH_LZ4_COMPRESSFRAME_USINGCDICT_TAKES_CCTX
framebench-all: FLAGS+=$(BROTLIFLAGS) $(ZSTDFLAGS) $(LZ4FLAGS) $(ZLIBFLAGS)
framebench-all: LDFLAGS+=$(BROTLILDFLAGS) $(ZSTDLDFLAGS) $(LZ4LDFLAGS) $(ZLIBLDFLAGS)
framebench-all: framebench.c liblz4.a libzstd.a libbrotlicommon-static.a libbrotlidec-static.a libbrotlienc-static.a
	$(CC) $(FLAGS) -o framebench-all framebench.c liblz4.a libzstd.a libbrotlidec-static.a libbrotlienc-static.a libbrotlicommon-static.a $(LDFLAGS)


regressiontest : regressiontest.c libzstd.a
	$(CC) $(FLAGS) $(ZSTDINCLUDES) -o regressiontest regressiontest.c libzstd.a $(LDFLAGS)

.PHONY: clean
clean:
	rm -f framebench framebench-zstd framebench-lz4 liblz4.a libzstd.a

.PHONY: clean-zstd
clean-zstd:
	$(MAKE) -C $(ZSTDDIR) clean

.PHONY: clean-lz4
clean-lz4:
	$(MAKE) -C $(LZ4DIR) clean

.PHONY: clean-brotli
clean-brotli:
	$(MAKE) -C $(BROTLIDIR) clean
