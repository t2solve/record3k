include(../../config/common.pri)

TEMPLATE = app
TARGET   = rerun
DESTDIR  = ../../bin

INCLUDEPATH += include

# liblbt (ProcessingPipeline, PipelineConfigLoader, ResultLogger, FrameMemoryObject, ...)
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$OUT_PWD/../../lib -L$$PWD/../../lib -llbt -Wl,-rpath,\'\$$ORIGIN/../lib\'

# SQLite (for optional DebugLogger flush via ProcessingPipeline::flushDebugLogger)
LIBS += -lsqlite3

# std::thread / StepLogger callbacks
LIBS += -lpthread

SOURCES += \
    src/main.cpp \
    src/sequential_frame_source.cpp

HEADERS += \
    include/sequential_frame_source.h
