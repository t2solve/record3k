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
    src/rerun \
    #src/pipelineviewer

# feature/simulation: pipeoptimizer + rerun are built by default on this branch.

# Legacy test programs (test2-test7, debugcameraload, appgentestvideo) live on
# branch archive/test-targets.

#define deps
src/pipeoptimizer.depends = src/liblbt
src/pipelineviewer.depends = src/liblbt
src/rerun.depends = src/liblbt


# Build API app only when CONFIG+=api is set
contains(CONFIG, api) {
    SUBDIRS += src/api
    src/api.depends = src/liblbt
}
