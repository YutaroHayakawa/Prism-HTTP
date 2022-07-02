TOPDIR:=../../..

LDLIBS:= \
	-lprism-switch-client \
	`pkg-config --libs protobuf` \
	-luv \
	-ltomcrypt \
	-ltfm \
	-lleveldb \
	-lssl \
	-lcrypto

include $(TOPDIR)/src/Makefile.inc

OBJS:=phttp_kvs_repl_backend.o phttp_kvs_repl_proxy.o
TARGETS:= phttp-kvs-repl-backend phttp-kvs-repl-proxy

all: $(TARGETS)

phttp-kvs-repl-backend: phttp_kvs_repl_backend.o $(TOPDIR)/src/libphttp.a
	$(CXX) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

phttp-kvs-repl-proxy: phttp_kvs_repl_proxy.o $(TOPDIR)/src/libphttp.a
	$(CXX) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

install:
	install phttp-kvs-repl-proxy /usr/local/bin
	install phttp-kvs-repl-backend /usr/local/bin

clean:
	- rm $(TARGETS) $(OBJS)
