add_library(HomeAutomationGesture STATIC
    gesture_gate.cpp
    gesture_gate.h
    gesture_model.cpp
    gesture_model.h
    mediapipe_stream_client.cpp
    mediapipe_stream_client.h
    gesture_pipeline.cpp
    gesture_pipeline.h
    gesture_types.h
)

target_include_directories(HomeAutomationGesture
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(HomeAutomationGesture
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)
