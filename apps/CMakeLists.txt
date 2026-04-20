add_executable(HomeAutomationApp
    main.cpp
    app_resources.qrc
)

target_link_libraries(HomeAutomationApp
    PRIVATE
        HomeAutomationUI
)

set(_gesture_model_source "${PROJECT_SOURCE_DIR}/resources/models/cpp_model.json")
if(EXISTS "${_gesture_model_source}")
    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HomeAutomationApp>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_gesture_model_source}"
            "$<TARGET_FILE_DIR:HomeAutomationApp>/models/cpp_model.json"
        COMMENT "Copying C++ gesture model for runtime inference"
        VERBATIM
    )
endif()

set(_runtime_setting_source "${PROJECT_SOURCE_DIR}/config/setting.json")
if(EXISTS "${_runtime_setting_source}")
    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HomeAutomationApp>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_runtime_setting_source}"
            "$<TARGET_FILE_DIR:HomeAutomationApp>/config/setting.json"
        COMMENT "Copying runtime setting.json for UI/style configuration"
        VERBATIM
    )
endif()

set(_hand_landmarker_task_source "${PROJECT_SOURCE_DIR}/resources/models/hand_landmarker.task")
if(EXISTS "${_hand_landmarker_task_source}")
    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HomeAutomationApp>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_hand_landmarker_task_source}"
            "$<TARGET_FILE_DIR:HomeAutomationApp>/models/hand_landmarker.task"
        COMMENT "Copying MediaPipe hand_landmarker.task for runtime tracking"
        VERBATIM
    )
endif()

set(_home_obj_source "${PROJECT_SOURCE_DIR}/resources/models/home.obj")
if(EXISTS "${_home_obj_source}")
    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HomeAutomationApp>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_home_obj_source}"
            "$<TARGET_FILE_DIR:HomeAutomationApp>/models/home.obj"
        COMMENT "Copying home.obj model for OpenGL viewport"
        VERBATIM
    )
endif()

set(_mediapipe_bridge_source "${PROJECT_SOURCE_DIR}/tools/hand_landmarker_stream.exe")
if(EXISTS "${_mediapipe_bridge_source}")
    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HomeAutomationApp>/tools"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_mediapipe_bridge_source}"
            "$<TARGET_FILE_DIR:HomeAutomationApp>/tools/hand_landmarker_stream.exe"
        COMMENT "Copying MediaPipe bridge executable for runtime tracking"
        VERBATIM
    )
endif()

set(_mediapipe_opencv_source "${PROJECT_SOURCE_DIR}/tools/opencv_world4110.dll")
if(EXISTS "${_mediapipe_opencv_source}")
    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HomeAutomationApp>/tools"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_mediapipe_opencv_source}"
            "$<TARGET_FILE_DIR:HomeAutomationApp>/tools/opencv_world4110.dll"
        COMMENT "Copying MediaPipe bridge OpenCV runtime dependency"
        VERBATIM
    )
    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_mediapipe_opencv_source}"
            "$<TARGET_FILE_DIR:HomeAutomationApp>/opencv_world4110.dll"
        COMMENT "Copying OpenCV runtime dependency for UI camera preview"
        VERBATIM
    )
endif()


if(WIN32 AND QT_VERSION_MAJOR EQUAL 6)
    get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)
    find_program(_qt_windeployqt_executable windeployqt HINTS "${_qt_bin_dir}" REQUIRED)

    add_custom_command(TARGET HomeAutomationApp POST_BUILD
        COMMAND "${_qt_windeployqt_executable}" --$<IF:$<CONFIG:Debug>,debug,release> "$<TARGET_FILE:HomeAutomationApp>"
        COMMENT "Deploying Qt runtime dependencies for HomeAutomationApp"
        VERBATIM
    )
endif()
