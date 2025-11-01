TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = \
    src/appgentestvideo \ 
    src/liblbt \
    src/test2 \
    src/test3 
    
#define deps
src/appgentestvideo.depends = src/liblbt
src/test2.depends = src/liblbt
src/test3.depends = src/liblbt
