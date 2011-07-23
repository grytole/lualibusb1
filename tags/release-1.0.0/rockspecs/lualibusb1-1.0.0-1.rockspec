package = "lualibusb1"
version = "1.0.0-1"
source = {
  url = "http://lualibusb1.googlecode.com/files/lualibusb1-1.0.0.tar.gz"
}
description = {
  summary = "libusb-1.0 binding",
  homepage = "http://lualibusb1.googlecode.com/",
  license = "MIT/X11"
}
dependencies = {
  "lua >= 5.1"
}
external_dependencies = {
  LIBUSB = {
    header = "libusb-1.0/libusb.h"
  }
}
supported_platforms = { "linux", "freebsd", "macosx" }
build = {
  type = "builtin",
  modules = {
    libusb1 = {
      sources = {"lusb.c"},
      libraries = {"usb-1.0"},
      incdirs = {"$(LIBUSB_INCDIR)/libusb-1.0"},
      libdirs = {"$(LIBUSB_LIBDIR)"}
    }
  }
}

