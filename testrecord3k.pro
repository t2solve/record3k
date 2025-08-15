TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = \
    src\appgentestvideo \ 
    src\liblbt \
    src\test2 

#define deps
src/appgentestvideo.depends = src/liblbt
src/test2.depends = src/liblbt
