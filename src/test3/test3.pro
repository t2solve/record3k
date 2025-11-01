include(../../obj/common.pri)


TEMPLATE = app
TARGET = test3recorder
DESTDIR = ../../bin

# Use liblbt headers and link to the built .so
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$PWD/../../lib -llbt -Wl,-rpath,\'\$$ORIGIN/../lib\'

SOURCES += \
    main.cpp \
    disk_image_frame_source.cpp \

HEADERS += \
    iframe_source.h \
    disk_image_frame_source.h