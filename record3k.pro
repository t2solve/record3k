# SPDX-License-Identifier: MIT
# See LICENSE file in the project root for full license information.

TEMPLATE = subdirs
CONFIG += ordered

# Set log level based on build configuration
# Can be overridden: qmake LOG_LEVEL=INFO or LOG_LEVEL=WARN
# Valid values: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
isEmpty(LOG_LEVEL) {
    CONFIG(debug, debug|release) {
        LOG_LEVEL = DEBUG
    } else {
        LOG_LEVEL = INFO
    }
}
export(LOG_LEVEL)

SUBDIRS = \
    src/liblbt \
    src/pipeoptimizer \
    src/pipelineviewer \
    src/rerun

# Disabled targets (uncomment to include):
# src/test2
# src/test3
# src/test4
# src/test5
# src/test6
# src/test7
# src/debugcameraload

#define deps
src/test2.depends = src/liblbt
src/test3.depends = src/liblbt
src/test4.depends = src/liblbt
src/test5.depends = src/liblbt
src/test6.depends = src/liblbt
src/test7.depends = src/liblbt
src/pipeoptimizer = src/liblbt
src/pipelineviewer.depends = src/liblbt
src/debugcameraload.depends = src/liblbt
src/rerun.depends = src/liblbt


# Build API app only when CONFIG+=api is set
contains(CONFIG, api) {
    SUBDIRS += src/api
    src/api.depends = src/liblbt
}
