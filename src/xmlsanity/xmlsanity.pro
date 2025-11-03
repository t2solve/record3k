include(../../obj/common.pri)


TEMPLATE = app
TARGET = xmlsanity
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

DESTDIR = ../../bin

# Use liblbt headers and link to the built .so
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$OUT_PWD/../../lib -L$$PWD/../../lib -llbt -Wl,-rpath,\'\$$ORIGIN/../lib\'


SOURCES += \
    main.cpp

