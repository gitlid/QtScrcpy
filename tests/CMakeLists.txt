find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)
set(CORE ${CMAKE_CURRENT_LIST_DIR}/../QtScrcpy/QtScrcpyCore)
add_executable(actionmacro_integration
    actionmacro_integration.cpp
    ../QtScrcpy/ui/actionmacrodialog.cpp
    ../QtScrcpy/ui/actionmacrodialog.h
    ../QtScrcpy/ui/actionmacrohotkey.h
)
target_include_directories(actionmacro_integration PRIVATE
    ${CORE}/src/common
    ${CORE}/src/device/android
    ${CORE}/src/device/controller
    ${CORE}/src/device/controller/inputconvert
)
target_link_libraries(actionmacro_integration PRIVATE
    QtScrcpyCore Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network
)
foreach(case_name IN ITEMS video_required stop_clears_queue transport_failure playback_guards disconnected_dialog)
    add_test(NAME integration_${case_name} COMMAND actionmacro_integration ${case_name})
    set_tests_properties(integration_${case_name} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(uhid_integration uhid_integration.cpp)
target_include_directories(uhid_integration PRIVATE
    ${CORE}/src/common ${CORE}/src/device/android ${CORE}/src/device/controller
    ${CORE}/src/device/controller/inputconvert ${CORE}/src/device/controller/receiver)
target_link_libraries(uhid_integration PRIVATE
    QtScrcpyCore Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
foreach(case_name IN ITEMS wire mapping rollover schema feedback lifecycle macro emergency game_mapping)
    add_test(NAME uhid_${case_name} COMMAND uhid_integration ${case_name})
    set_tests_properties(uhid_${case_name} PROPERTIES TIMEOUT 15)
endforeach()

if(MSVC)
    target_compile_options(uhid_integration PRIVATE /utf-8 /W3 /WX)
    target_compile_options(actionmacro_integration PRIVATE /utf-8)
endif()
