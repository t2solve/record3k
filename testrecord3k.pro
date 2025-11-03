TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = \
    src/appgentestvideo \
    src/liblbt \
    src/test2 \
    src/test3 \
    src/pipelineviewer \
    src/xmlsanity
    
#define deps
src/appgentestvideo.depends = src/liblbt
src/test2.depends = src/liblbt
src/test3.depends = src/liblbt
src/pipelineviewer.depends = src/liblbt
src/xmlsanity.depends = src/liblbt
