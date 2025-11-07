TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = \
    src/liblbt \
    src/test3 \
    src/pipelineviewer \
    #src/test2 \
    #src/appgentestvideo \
    #src/xmlsanity
    
#define deps
src/test3.depends = src/liblbt
src/pipelineviewer.depends = src/liblbt
#src/api.depends = src/liblbt

# Build API app only when CONFIG+=api is set
contains(CONFIG, api) {
    SUBDIRS += src/api
    src/api.depends = src/liblbt
}
#src/test2.depends = src/liblbt
#src/appgentestvideo.depends = src/liblbt
#src/xmlsanity.depends = src/liblbt
