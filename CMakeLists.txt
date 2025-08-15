cmake_minimum_required(VERSION 3.16)
project(ImageProcessingTests)

# Set C++ standard (matching your .pro file)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set policy to avoid warnings
cmake_policy(SET CMP0012 NEW)

# Find packages
find_package(GTest REQUIRED)
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem thread)

# Find Qt6 (or Qt5 if you prefer)
find_package(Qt6 REQUIRED COMPONENTS Core)
# If you need Qt5 instead, use:
# find_package(Qt5 REQUIRED COMPONENTS Core)

# Use pkg-config for OpenCV4 (matching your .pro file)
pkg_check_modules(OPENCV4 REQUIRED opencv4)

# Include directories
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${OPENCV4_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# VimbaX includes
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/vimbax/api/include)

# Drogon includes
include_directories(/usr/local/include)

# Compiler flags (matching your .pro file)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-enum-enum-conversion")

# Add definitions
add_definitions(-DQT_USE_QSTRINGBUILDER)
add_definitions(-DCONSOLE)

# VimbaX library paths
set(VIMBA_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vimbax/api/lib)

# Enable testing
enable_testing()

# Add subdirectory for tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()

# Add source code as a library
file(GLOB_RECURSE SRC_FILES "src/*.cpp")
if(SRC_FILES)
    add_library(imageprocessing_lib ${SRC_FILES})
    
    # Link libraries to the main library
    target_link_libraries(imageprocessing_lib 
        ${OPENCV4_LIBRARIES}
        ${Boost_LIBRARIES}
        ${VIMBA_LIB_DIR}/libVmbC.so
        ${VIMBA_LIB_DIR}/libVmbCPP.so
        Qt6::Core
        drogon
        jsoncpp
        pthread
    )
    
    # Set RPATH for VimbaX libraries
    set_target_properties(imageprocessing_lib PROPERTIES
        INSTALL_RPATH "${VIMBA_LIB_DIR}"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Custom target to copy VimbaX libraries (matching QMAKE_POST_LINK)
add_custom_target(copy_vimba_libs ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${VIMBA_LIB_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/bin
    COMMENT "Copying VimbaX libraries to bin directory"
)

# Set output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)