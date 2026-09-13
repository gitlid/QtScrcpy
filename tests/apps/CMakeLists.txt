cmake_minimum_required(VERSION 3.19)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ApplicationSessionRegression LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    enable_testing()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)
    set(APP_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../QtScrcpy)
    add_library(qsc_applications STATIC
        ${APP_ROOT}/ui/appsession.cpp ${APP_ROOT}/ui/appsession.h ${APP_ROOT}/ui/appbar.cpp ${APP_ROOT}/ui/appbar.h
        ${APP_ROOT}/QtScrcpyCore/include/QtScrcpyCore.h ${APP_ROOT}/QtScrcpyCore/include/adbprocess.h
        ${APP_ROOT}/QtScrcpyCore/src/adb/adbprocess.cpp ${APP_ROOT}/QtScrcpyCore/src/adb/adbprocessimpl.cpp
        ${APP_ROOT}/QtScrcpyCore/src/adb/adbprocessimpl.h)
    target_include_directories(qsc_applications PRIVATE ${APP_ROOT}/QtScrcpyCore/include)
    target_link_libraries(qsc_applications PUBLIC Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
    if(MSVC)
        target_compile_options(qsc_applications PRIVATE /utf-8 /W3 /WX)
    endif()
endif()
add_executable(app_session_tests app_session_test.cpp)
set_target_properties(app_session_tests PROPERTIES AUTOMOC ON CXX_STANDARD 11)
target_link_libraries(app_session_tests PRIVATE qsc_applications)
if(MSVC)
    target_compile_options(app_session_tests PRIVATE /utf-8 /W3 /WX)
endif()
foreach(case_name IN ITEMS parsers binding_metadata profile_switch profile_restart corrupt_profile launch_before_play
    switch_back stop_pending manual_pause missing_app launch_failure failed_probe disconnect hotkey_pending tabs_layout
    geometry_restore stalled_probe unreachable map_after_playback video_hit_area)
    add_test(NAME app_${case_name} COMMAND app_session_tests ${case_name})
    set_tests_properties(app_${case_name} PROPERTIES TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

add_executable(app_tabs_polish_tests app_tabs_polish_test.cpp)
set_target_properties(app_tabs_polish_tests PROPERTIES AUTOMOC ON CXX_STANDARD 11)
target_include_directories(app_tabs_polish_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../QtScrcpy/QtScrcpyCore/include)
target_link_libraries(app_tabs_polish_tests PRIVATE qsc_applications)
if(MSVC)
    target_compile_options(app_tabs_polish_tests PRIVATE /utf-8 /W3 /WX)
endif()
foreach(case_name IN ITEMS name_utf8 name_wrapped name_diagnostics output_channels cache_unbound cache_device name_retry name_failure
    ordinary_switch macro_switch repeated_visits navigation_queue desktop_visit stop_visit pause_visit pause_recovery visit_failure
    mapping_guard overflow_menu overflow_navigation overflow_guard literal_name)
    add_test(NAME apps_polish_${case_name} COMMAND app_tabs_polish_tests ${case_name})
    set_tests_properties(apps_polish_${case_name} PROPERTIES TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
