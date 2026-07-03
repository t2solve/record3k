include(../../obj/common.pri)

TEMPLATE = app
TARGET   = pipeoptimizer
DESTDIR  = ../../bin

INCLUDEPATH += include

# liblbt (for ProcessingPipeline, PipelineConfigLoader, FrameMemoryObject)
INCLUDEPATH += $$PWD/../liblbt/include
LIBS += -L$$OUT_PWD/../../lib -L$$PWD/../../lib -llbt -Wl,-rpath,\'\$$ORIGIN/../lib\'

# SQLite (libsqlite3-dev installed in Dockerfile)
LIBS += -lsqlite3

# std::thread
LIBS += -lpthread

SOURCES += \
    src/main.cpp \
    src/synthetic_generator.cpp \
    src/metrics.cpp \
    src/sim_db.cpp \
    src/pipe_config_gen.cpp \
    src/sim_runner.cpp

HEADERS += \
    include/synth_config.h \
    include/synthetic_generator.h \
    include/metrics.h \
    include/sim_db.h \
    include/pipe_config_gen.h \
    include/sim_runner.h
