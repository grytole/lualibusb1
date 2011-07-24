VERSION= 1.0.1

BASEDIR= /home/tnharris
INSTALL_LIB= $(BASEDIR)/lib/lua/5.2
LUAINC= -I$(BASEDIR)/include
LUALIB= -L$(BASEDIR)/lib -llua
ifneq "$(shell pkg-config --version)" ""
    USBINC= $(shell pkg-config --cflags libusb-1.0)
    USBLIB= $(shell pkg-config --libs libusb-1.0)
else
    USBINC= -I$(BASEDIR)/include/libusb-1.0
    USBLIB= -L$(BASEDIR)/lib -lusb-1.0
endif

CC= gcc -g
CFLAGS= -O0 -Wall -fPIC $(LUAINC) $(USBINC)
LDFLAGS= -shared -fPIC
LIBS= $(USBLIB)
ENV=
ifeq "$(shell uname)" "Darwin"
    LDFLAGS= -bundle -undefined dynamic-lookup
    ENV= MACOSX_DEPLOYMENT_TARGET=10.4
endif
INSTALL= install -p -m 0755

libusb1.so: lusb.o
	env $(ENV) $(CC) $(LDFLAGS) $(LIBS) lusb.o -o libusb1.so

lusb.o: lusb.c

install: libusb1.so
	$(INSTALL) libusb1.so $(INSTALL_LIB)

tar: dist/lualibusb1-$(VERSION).tar.gz

dist/lualibusb1-$(VERSION).tar.gz: libusb1.so rockspecs/lualibusb1-$(VERSION)-1.rockspec
	mkdir -p dist/lualibusb1-$(VERSION)
	mkdir -p dist/lualibusb1-$(VERSION)/rockspecs
	cp README COPYRIGHT Makefile lusb.c dist/lualibusb1-$(VERSION)
	cp rockspecs/*.rockspec dist/lualibusb1-$(VERSION)/rockspecs
	tar -cz -C dist -f $@ lualibusb1-$(VERSION)
	rm -r dist/lualibusb1-$(VERSION)

rockspec: rockspecs/lualibusb1-$(VERSION)-1.rockspec

rockspecs/lualibusb1-$(VERSION)-1.rockspec: lualibusb1.rockspec
	mkdir -p rockspecs
	env VERSION=$(VERSION) lua pp.lua $^ $@

