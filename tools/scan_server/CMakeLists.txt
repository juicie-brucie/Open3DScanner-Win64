add_executable(scan_server main.cpp)
target_link_libraries(scan_server PRIVATE openscanner_engine)

# Hardware-free smoke test for the tracking-verdict name rendering (row #91).
add_executable(tracking_status_test tracking_status_test.cpp)
target_link_libraries(tracking_status_test PRIVATE openscanner_engine)
add_test(NAME tracking_status_test COMMAND tracking_status_test)

# Hardware-free smoke test for the per-call bound protocol (row #89).
add_executable(bounded_watchdog_test bounded_watchdog_test.cpp)
target_link_libraries(bounded_watchdog_test PRIVATE openscanner_engine)
add_test(NAME bounded_watchdog_test COMMAND bounded_watchdog_test)

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(scan_server PRIVATE ${CORE_FOUNDATION_LIBRARY})

    # --live mode runs a real OBScan session; same crash/hang isolation
    # story as live_capture_test (see vulkan_disable_interpose.c /
    # worker_process.cpp).
    add_dependencies(scan_server vulkan_disable_interpose)
    target_compile_definitions(scan_server PRIVATE
        VULKAN_DISABLE_INTERPOSE_DYLIB="$<TARGET_FILE:vulkan_disable_interpose>"
    )

    # Row #107: refresh the app bundle's copy of this worker whenever it
    # rebuilds. Attached here (add_custom_command(TARGET ...) must live in the
    # target's own directory) rather than on OpenScanner, whose link step CMake
    # skips when only the worker changed — which left the bundled copy stale.
    # The script no-ops when no bundle exists yet; OpenScanner's own POST_BUILD
    # populates it on a clean/app build.
    #
    # Fork: this tree does not take tools/macos_app/ or the OpenScanner app
    # target (see docs/recomp/PLAN.md, sync scope). Gate the refresh on both so
    # macOS CI still configures here; it comes back if the app is ever taken.
    if(TARGET OpenScanner AND EXISTS "${CMAKE_SOURCE_DIR}/tools/macos_app/install_bundle.cmake")
        add_custom_command(TARGET scan_server POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    "-DAPP_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:OpenScanner>"
                    "-DWORKER=$<TARGET_FILE:scan_server>"
                    "-DUI_DIR=${CMAKE_SOURCE_DIR}/ui"
                    -P "${CMAKE_SOURCE_DIR}/tools/macos_app/install_bundle.cmake"
            COMMENT "Refreshing the app bundle's scan_server (worker rebuild)"
        )
    endif()
endif()
