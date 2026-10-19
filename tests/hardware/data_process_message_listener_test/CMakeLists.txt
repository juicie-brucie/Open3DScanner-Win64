add_executable(data_process_message_listener_test main.cpp)
target_link_libraries(data_process_message_listener_test PRIVATE openscanner_engine)

# Interposer: macOS-only (see device_message_listener_test/CMakeLists.txt). Windows
# builds this test WITHOUT an interposer; the launcher states that at run time.
if(WIN32)
    message(STATUS "data_process_message_listener_test: no interposer on Windows (macOS-only mechanism)")
endif()

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(data_process_message_listener_test PRIVATE ${CORE_FOUNDATION_LIBRARY})

    add_dependencies(data_process_message_listener_test vulkan_disable_interpose)
    target_compile_definitions(data_process_message_listener_test PRIVATE
        VULKAN_DISABLE_INTERPOSE_DYLIB="$<TARGET_FILE:vulkan_disable_interpose>"
    )
endif()

add_test(NAME data_process_message_listener_test COMMAND data_process_message_listener_test)
set_tests_properties(data_process_message_listener_test PROPERTIES
    LABELS "hardware"
    SKIP_RETURN_CODE 2)
