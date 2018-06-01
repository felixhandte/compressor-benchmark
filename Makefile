
LZ4DIR ?= /home/felix/prog/lz4
LZ4LIBDIR ?= $(LZ4DIR)/lib
LZ4HDRDIR ?= $(LZ4DIR)/lib

LZ42DIR ?= /home/felix/prog/lz4-2

ZSTDDIR ?= /home/felix/prog/zstd
ZSTDLIBDIR ?= $(ZSTDDIR)/lib
ZSTDHDRDIR ?= $(ZSTDDIR)/lib

ZSTD2DIR ?= /home/felix/prog/zstd-2

ZSTDFLAGS = -DBENCH_ZSTD -I$(ZSTDLIBDIR)
LZ4FLAGS = -DBENCH_LZ4 -I$(LZ4LIBDIR)

CFLAGS  ?= -O3 -DNDEBUG -march=native -mtune=native
DEBUGFLAGS:= -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow \
             -Wswitch-enum -Wdeclaration-after-statement -Wstrict-prototypes \
             -Wundef -Wpointer-arith -Wstrict-aliasing=1
CFLAGS  += $(DEBUGFLAGS) $(MOREFLAGS)
FLAGS    = $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)

.PHONY: all
all: framebench

.PHONY: $(LZ4LIBDIR)/liblz4.a
$(LZ4LIBDIR)/liblz4.a:
	$(MAKE) -C $(LZ4LIBDIR) liblz4.a

.PHONY: $(ZSTDLIBDIR)/libzstd.a
$(ZSTDLIBDIR)/libzstd.a:
	$(MAKE) -C $(ZSTDLIBDIR) libzstd.a

liblz4.a: $(LZ4LIBDIR)/liblz4.a
	cp $(LZ4LIBDIR)/liblz4.a liblz4.a

libzstd.a: $(ZSTDLIBDIR)/libzstd.a
	cp $(ZSTDLIBDIR)/libzstd.a libzstd.a

framebench: framebench.c liblz4.a libzstd.a
	$(CC) $(FLAGS) -I$(LZ4LIBDIR) -I$(ZSTDLIBDIR) -o framebench framebench.c liblz4.a libzstd.a

framebench-lz4: LZ4FLAGS+=$(FLAGS)
framebench-lz4: framebench.c liblz4.a
	$(CC) $(LZ4FLAGS) -o framebench-lz4 framebench.c liblz4.a

framebench-zstd: ZSTDFLAGS+=$(FLAGS)
framebench-zstd: framebench.c libzstd.a
	$(CC) $(ZSTDFLAGS) -o framebench-zstd framebench.c libzstd.a

.PHONY: zstdcompare
zstdcompare: framebench-zstd-dev framebench-zstd-exp

framebench-zstd-exp framebench-zstd-dev: MOREFLAGS="-O3 -march=native -mtune=native -ggdb -DBENCH_TARGET_NANOSEC=250000000ull -DNDEBUG"
# framebench-zstd-exp framebench-zstd-dev: MOREFLAGS="-Og -ggdb -DZSTD_DEBUG=6"
framebench-zstd-exp framebench-zstd-dev: CC?="gcc"

.PHONY: framebench-zstd-exp
framebench-zstd-exp:
	make clean
	make clean-zstd ZSTDDIR=$(ZSTDDIR)
	make framebench-zstd -j32 ZSTDDIR=$(ZSTDDIR) CC=$(CC) MOREFLAGS=$(MOREFLAGS)
	mv framebench-zstd framebench-zstd-exp

.PHONY: framebench-zstd-dev
framebench-zstd-dev:
	make clean
	make clean-zstd ZSTDDIR=$(ZSTD2DIR)
	make framebench-zstd -j32 ZSTDDIR=$(ZSTD2DIR) CC=$(CC) MOREFLAGS=$(MOREFLAGS)
	mv framebench-zstd framebench-zstd-dev


.PHONY: clean
clean:
	rm -f framebench framebench-zstd framebench-lz4 liblz4.a libzstd.a

.PHONY: clean-zstd
clean-zstd:
	$(MAKE) -C $(ZSTDLIBDIR) clean

.PHONY: clean-lz4
clean-lz4:
	$(MAKE) -C $(LZ4LIBDIR) clean