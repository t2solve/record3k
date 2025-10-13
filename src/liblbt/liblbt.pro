TARGET = liblbt
VERSION = 0.0.2
TEMPLATE = lib

# include common settings
include(../../obj/common.pri)

# For shared library
CONFIG += shared

DESTDIR = ../../lib

# Public headers exported by the library
INCLUDEPATH += $$PWD/include
HEADERS += \
    include/liblbt/process_step_factory.h \
    include/liblbt/processing_pipeline.h \
    include/liblbt/frame_memory_object.h \
    include/liblbt/pipeline_profiler.h \
    include/liblbt/pipeline_config_loader.h

# Or for static library
# CONFIG += static
