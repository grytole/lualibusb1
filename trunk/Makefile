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

