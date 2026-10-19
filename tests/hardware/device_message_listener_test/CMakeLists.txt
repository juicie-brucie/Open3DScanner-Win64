add_executable(device_message_listener_test main.cpp)
add_test(NAME device_message_listener_test COMMAND device_message_listener_test)
set_tests_properties(device_message_listener_test PROPERTIES
    LABELS "hardware"
    SKIP_RETURN_CODE 2)
target_link_libraries(device_message_listener_test PRIVATE openscanner_engine)

# Interposer: macOS-only. On macOS the launcher inserts vulkan_disable_interpose
# (a dylib built only there) via DYLD_INSERT_LIBRARIES to stop the vendor's
# Vulkan init from hanging. Windows has no DYLD_INSERT_LIBRARIES equivalent and
# builds no interposer target, so this test is built WITHOUT one; its launcher
# prints that explicitly at run time. A Windows run is therefore NOT equivalent
# to a Mac run on this axis - do not record it as if it were.
if(WIN32)
    message(STATUS "device_message_listener_test: no interposer on Windows (macOS-only mechanism)")
    target_link_libraries(device_message_listener_test PRIVATE setupapi)
endif()

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(device_message_listener_test PRIVATE ${CORE_FOUNDATION_LIBRARY})

    add_dependencies(device_message_listener_test vulkan_disable_interpose)
    target_compile_definitions(device_message_listener_test PRIVATE
        VULKAN_DISABLE_INTERPOSE_DYLIB="$<TARGET_FILE:vulkan_disable_interpose>"
    )
endif()