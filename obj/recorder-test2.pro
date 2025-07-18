TEMPLATE = app

QT += core
CONFIG += c++20 warn_on console

INCLUDEPATH += ../src
SRC_DIR=../src
SOURCES += 	$$SRC_DIR/test2/*.cpp \
			$$SRC_DIR/steps/*.cpp \
			$$SRC_DIR/*.cpp 
HEADERS += 	$$SRC_DIR/test2/*.h \
			$$SRC_DIR/steps/*.h \ 
			$$SRC_DIR/*.h  

DEFINES *= QT_USE_QSTRINGBUILDER		#converts + to % when building strings 	#append macro
DEFINES += CONSOLE 	#add macro
DEFINES += CUDA_ENABLED #enable code 

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

# Drogon (adjust the paths if Drogon is installed elsewhere)
#INCLUDEPATH += /usr/local/include
#LIBS += -L/usr/local/lib -ldrogon

# If Drogon depends on other libraries (e.g., pthread, boost, jsoncpp, etc.), add them as well:
#LIBS += -ljsoncpp -lboost_system -lboost_filesystem -lboost_thread -lpthread

TARGET = test2
DESTDIR = ../bin
