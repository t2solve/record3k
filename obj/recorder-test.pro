TEMPLATE = app

QT += core
CONFIG += c++20 warn_on console

INCLUDEPATH += ../src
SRC_DIR=../src
SOURCES += 	$$SRC_DIR/test/*.cpp \
				$$SRC_DIR/*.cpp
HEADERS += 	$$SRC_DIR/test/*.h \
				$$SRC_DIR/*.h

DEFINES *= QT_USE_QSTRINGBUILDER		#converts + to % when building strings 	#append macro
DEFINES += CONSOLE 	#add macro

#DEFINES += DEBUG

# opencv
CONFIG += link_pkgconfig
PKGCONFIG += opencv4

# VimbaX
INCLUDEPATH += ../vimbax/api/include
VIMBA_LIB_DIR = ../vimbax/api/lib
LIBS += -L$${VIMBA_LIB_DIR} -lVmbC -lVmbCPP -Wl,-rpath,\'\$$ORIGIN\'

#-Wl,-rpath,.
QMAKE_POST_LINK += cp $${VIMBA_LIB_DIR}/lib*.so ../bin  # copy vmb-libs to bin
QMAKE_CXXFLAGS += -Wno-deprecated-enum-enum-conversion # ignore opencv warnings.


TARGET = test
DESTDIR = ../bin


