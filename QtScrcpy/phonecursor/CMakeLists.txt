find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_package(Java 17 REQUIRED COMPONENTS Runtime)
set(cursor_jar "${CMAKE_CURRENT_BINARY_DIR}/qtscrcpy-cursor.jar")
add_custom_command(OUTPUT "${cursor_jar}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/ci/build_phone_cursor.py"
        --java "${Java_JAVA_EXECUTABLE}" --source "${CMAKE_CURRENT_SOURCE_DIR}/PhoneCursor.java"
        --output "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS PhoneCursor.java "${CMAKE_SOURCE_DIR}/ci/build_phone_cursor.py" VERBATIM)
add_custom_target(phone_cursor_helper DEPENDS "${cursor_jar}")
add_dependencies(QtScrcpy phone_cursor_helper)
add_custom_command(TARGET phone_cursor_helper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:QtScrcpy>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cursor_jar}" "$<TARGET_FILE_DIR:QtScrcpy>/qtscrcpy-cursor.jar")
