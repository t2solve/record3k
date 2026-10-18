QT += core
CONFIG += c++20 warn_on console

INCLUDEPATH += ../src
SRC_DIR = ../src

DEFINES *= QT_USE_QSTRINGBUILDER     #converts + to % when building strings
DEFINES += CONSOLE

# OpenCV
CONFIG += link_pkgconfig
PKGCONFIG += opencv4 # jsoncpp drogon

PROJECT_ROOT = $$clean_path($$PWD/..)

# Optional features via CONFIG flags:
#  - CONFIG+=vimbax       Enable Vimba X SDK integration (adds include/libs, defines VIMBAX_ENABLED)
#  - CONFIG+=cuda         Enable CUDA paths in code (defines CUDA_ENABLED)
#  - CONFIG+=api         Enable drogon api interface build

# CUDA toggle
contains(CONFIG, cuda) {
    DEFINES += CUDA_ENABLED
    message( [config] CUDA enabled )
} else {
    message( [config] CUDA disabled )
}

# VimbaX toggle
contains(CONFIG, vimbax) {
    DEFINES += VIMBAX_ENABLED
    INCLUDEPATH += $$PROJECT_ROOT/vimbax/api/include
    VIMBA_LIB_DIR = $$PROJECT_ROOT/vimbax/api/lib
    LIBS += -L$${VIMBA_LIB_DIR} -lVmbC -lVmbCPP -Wl,-rpath,\'\$$ORIGIN\'

    # Copy runtime libs only for app targets into their output dir
    contains(TEMPLATE, app) {
        isEmpty(DESTDIR) { DESTDIR = $$PROJECT_ROOT/bin }
        QMAKE_POST_LINK += $$QMAKE_MKDIR_CMD $$DESTDIR && cp -v $${VIMBA_LIB_DIR}/lib*.so $$DESTDIR
    }
    message( [config] VimbaX enabled )
} else {
    message( [config] VimbaX disabled )
}

# liblbt public headers for all targets
INCLUDEPATH += $$PROJECT_ROOT/src/liblbt/include
# Toggle mock data for InfoController (uncomment to enable mocks)
# DEFINES += DEBUG_FLAG_MOCK

QMAKE_CXXFLAGS += -Wno-deprecated-enum-enum-conversion

# Add system /usr/local to rpath so libraries installed there are found at runtime
QMAKE_RPATHDIR += /usr/local/lib

# api toggle
contains(CONFIG, api) {
    DEFINES += API_ENABLED
    message( [config] api enabled )

    # Prefer pkg-config (brings in -ldrogon -ltrantor -ljsoncpp -lssl -lcrypto -lz ... as needed)
    DROGON_PKG_OK = $$system(pkg-config --exists drogon && echo yes || echo no)
    equals(DROGON_PKG_OK, yes) {
        CONFIG += link_pkgconfig
        PKGCONFIG += drogon
        # jsoncpp is usually pulled by drogon.pc; uncomment if needed:
        PKGCONFIG += jsoncpp
    } else {
        message( [config] drogon via pkg-config not found; falling back to /usr/local )
        INCLUDEPATH += /usr/local/include
        INCLUDEPATH += /usr/include/jsoncpp #json
        LIBS += -L/usr/local/lib -ldrogon -ltrantor -ljsoncpp -lssl -lcrypto -lz -lpthread
        QMAKE_RPATHDIR += /usr/local/lib
    }

    # App-only post link (keep empty unless you need to copy runtime libs)
    contains(TEMPLATE, app) {
        isEmpty(DESTDIR) { DESTDIR = $$PROJECT_ROOT/bin }
    }
} else {
    message( [config] api disabled )
}

