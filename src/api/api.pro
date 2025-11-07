include(../../obj/common.pri)

TEMPLATE = app
TARGET = apiserver
DESTDIR = ../../bin

# Use liblbt headers and link to the built .so
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$OUT_PWD/../../lib -L$$PWD/../../lib -llbt -Wl,-rpath,'$$ORIGIN/../lib'

SOURCES += \
    main.cpp \
    controllers/add_controller.cpp \
    controllers/do_controller.cpp \
    controllers/get_controller.cpp \
    controllers/info_controller.cpp \
    controllers/docs_controller.cpp \
    controllers/cors_controller.cpp
    
HEADERS += \
    controllers/add_controller.h \
    controllers/do_controller.h \
    controllers/get_controller.h \
    controllers/info_controller.h \
    controllers/docs_controller.h \
    controllers/cors_controller.h
    