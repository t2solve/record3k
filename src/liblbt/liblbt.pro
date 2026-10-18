TARGET = lbt
VERSION = 0.0.2
TEMPLATE = lib

# include common settings
include(../../config/common.pri)

# For shared library
CONFIG += shared

DESTDIR = ../../lib

# Public headers exported by the library
INCLUDEPATH += \
    $$PWD/../api/include \
    $$PWD/../api/include/api \
    $$PWD/../api/include/api/runtime \
    $$PWD/../api/include/api/controllers \
    $$PWD/include \
    $$PWD/include/liblbt \
    $$PWD/include/liblbt/steps \
    $$PWD/src \
    $$PWD/src/steps

SOURCES += \
    $$files($$PWD/src/*.cpp) \
    $$files($$PWD/src/steps/*.cpp) \
    $$files($$PWD/src/factory/*.cpp)

HEADERS += \
    $$files($$PWD/include/liblbt/*.h) \
    $$files($$PWD/include/liblbt/steps/*.h) \
    $$files($$PWD/include/liblbt/factory/*.h)

# Link jsoncpp for JsonStore used inside the library
CONFIG += link_pkgconfig
PKGCONFIG += jsoncpp

# sqlite3 (tag_data_store.cpp, steps/debug_logger.cpp): linked directly rather
# than via PKGCONFIG since not every distro's libsqlite3-dev ships a .pc file
# (matches pipeoptimizer.pro / rerun.pro, which already do this).
LIBS += -lsqlite3

# Or for static library
# CONFIG += static
