TARGET = liblbt
VERSION = 0.0.2
TEMPLATE = lib

# include common settings
include(../../obj/common.pri)

# For shared library
CONFIG += shared

DESTDIR = ../../lib

# Or for static library
# CONFIG += static
