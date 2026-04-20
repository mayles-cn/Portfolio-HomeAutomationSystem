add_library(HomeAutomationUI STATIC
    mainwidget.cpp
    mainwidget.h
    mainwidget_runtime.cpp
    mainwidget_support.cpp
    mainwidget_support.h
    widgets/frame_sequence_widget.cpp
    widgets/frame_sequence_widget.h
    widgets/camera_system.cpp
    widgets/camera_system.h
    widgets/lighting_system.cpp
    widgets/lighting_system.h
    widgets/model_opengl_widget.cpp
    widgets/model_opengl_widget.h
)

if(NOT OpenCV_DIR)
    set(_home_automation_default_opencv_dir "D:/SomeLibrarys/OpenCV4.11MSVC/build")
    if(EXISTS "${_home_automation_default_opencv_dir}/OpenCVConfig.cmake")
        set(OpenCV_DIR "${_home_automation_default_opencv_dir}" CACHE PATH "OpenCV config path")
    endif()
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

target_include_directories(HomeAutomationUI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/3rdparty/OBJloader/include
)

target_link_libraries(HomeAutomationUI
    PUBLIC
        HomeAutomationGesture
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::OpenGL
        ${OpenCV_LIBS}
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    target_link_libraries(HomeAutomationUI
        PUBLIC
            Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    )
endif()
