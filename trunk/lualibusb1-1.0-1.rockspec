package = "lualibusb1"
version = "1.0-1"
source = { url = "" }
description = {
  summary = "libusb-1.0 binding.",
  homepage = "",
  license = "MIT"
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

