
LZ4DIR ?= /home/felix/prog/lz4
LZ4LIBDIR ?= $(LZ4DIR)/lib
LZ4HDRDIR ?= $(LZ4DIR)/lib

ZSTDDIR ?= /home/felix/prog/zstd
ZSTDLIBDIR ?= $(ZSTDDIR)/lib
ZSTDHDRDIR ?= $(ZSTDDIR)/lib

ZSTDFLAGS = -DBENCH_ZSTD -I$(ZSTDLIBDIR)
LZ4FLAGS = -DBENCH_LZ4 -I$(LZ4LIBDIR)

CFLAGS  ?= -O3
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


.PHONY: clean
clean:
	rm -f framebench framebench-* liblz4.a libzstd.a