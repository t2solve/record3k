include(../../obj/common.pri)

QT += widgets

TEMPLATE = app
TARGET = pipelineviewer
DESTDIR = ../../bin

# Use liblbt headers and link to the built .so
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$OUT_PWD/../../lib -L$$PWD/../../lib -llbt -Wl,-rpath,\'\$$ORIGIN/../lib\'

SOURCES += \
    main.cpp \
    MainWindow.cpp

HEADERS += \
    MainWindow.h
