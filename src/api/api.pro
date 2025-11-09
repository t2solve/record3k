include(../../obj/common.pri)

TEMPLATE = app
TARGET = apiserver
DESTDIR = ../../bin

# Use liblbt headers and link to the built .so
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$OUT_PWD/../../lib -L$$PWD/../../lib -llbt -Wl,-rpath,'$$ORIGIN/../lib'

INCLUDEPATH += $$PWD/include $$PWD/include/api $$PWD/include/api/controllers $$PWD/include/api/runtime $$PWD/runtime

SOURCES += \
    main.cpp \
    controllers/add_controller.cpp \
    controllers/do_controller.cpp \
    controllers/get_controller.cpp \
    controllers/info_controller.cpp \
    controllers/docs_controller.cpp \
    controllers/cors_controller.cpp \
    runtime/info_data_manager.cpp

HEADERS += \
    include/api/controllers/add_controller.h \
    include/api/controllers/do_controller.h \
    include/api/controllers/get_controller.h \
    include/api/controllers/info_controller.h \
    include/api/controllers/docs_controller.h \
    include/api/controllers/cors_controller.h \
    include/api/runtime/info_data_manager.h
    
# Ensure jsoncpp headers are visible to compile public headers that include <json/json.h>
JSONCPP_PKG_OK = $$system(pkg-config --exists jsoncpp && echo yes || echo no)
equals(JSONCPP_PKG_OK, yes) {
    CONFIG += link_pkgconfig
    PKGCONFIG += jsoncpp
} else {
    INCLUDEPATH += /usr/include/jsoncpp
}