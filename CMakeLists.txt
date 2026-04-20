cmake_minimum_required(VERSION 3.16)

project(HomeAutomationSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Required by Qt on MSVC so __cplusplus reports the actual language level.
    add_compile_options(/Zc:__cplusplus)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGL)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGL)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS OpenGLWidgets)
endif()

if(MSVC)
    if((DEFINED Qt6_DIR AND Qt6_DIR MATCHES "mingw_64")
        OR (DEFINED QT_DIR AND QT_DIR MATCHES "mingw_64"))
        message(FATAL_ERROR
            "Detected MinGW Qt package with MSVC toolchain.\n"
            "Please reconfigure with Qt6_DIR pointing to an MSVC Qt kit, e.g.\n"
            "C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6")
    endif()
endif()

add_subdirectory(modules/gesture)
add_subdirectory(modules/ui)
add_subdirectory(apps)
 
