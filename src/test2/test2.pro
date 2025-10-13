include(../../obj/common.pri)


TEMPLATE = app
TARGET = test
DESTDIR = ../../bin

# Use liblbt headers and link to the built .so
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$PWD/../../lib -llbt -Wl,-rpath,\'\$$ORIGIN/../lib\'

SOURCES += \
    src/main.cpp \
    src/test_image_generator.cpp

#HEADERS += \
#    include/mainwindow.h