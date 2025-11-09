# SPDX-License-Identifier: MIT
# See LICENSE file in the project root for full license information.

TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = \
    src/liblbt \
    src/test3 \
    src/pipelineviewer \

    
#define deps
src/test3.depends = src/liblbt
src/pipelineviewer.depends = src/liblbt
#src/api.depends = src/liblbt

# Build API app only when CONFIG+=api is set
contains(CONFIG, api) {
    SUBDIRS += src/api
    src/api.depends = src/liblbt
}
