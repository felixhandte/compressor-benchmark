
LZ4DIR ?= /home/felixh/prog/lz4
LZ4LIBDIR ?= $(LZ4DIR)/lib
LZ4HDRDIR ?= $(LZ4DIR)/lib

ZSTDDIR ?= /home/felixh/prog/zstd
ZSTDLIBDIR ?= $(ZSTDDIR)/lib
ZSTDHDRDIR ?= $(ZSTDDIR)/lib


.PHONY: all
all: framebench

liblz4.a: $(LZ4LIBDIR)/liblz4.a
	$(MAKE) -C $(LZ4LIBDIR) liblz4.a
	cp $(LZ4LIBDIR)/liblz4.a liblz4.a

libzstd.a: $(ZSTDLIBDIR)/libzstd.a
	$(MAKE) -C $(ZSTDLIBDIR) libzstd.a
	cp $(ZSTDLIBDIR)/libzstd.a libzstd.a

framebench: framebench.c liblz4.a libzstd.a
	$(CC) -I$(LZ4LIBDIR) -I$(ZSTDLIBDIR) -o framebench framebench.c liblz4.a libzstd.a


.PHONY: clean
clean:
	rm -f framebench liblz4.a libzstd.a