cmake_minimum_required(VERSION 3.21)
project(qt-capture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
qt_standard_project_setup()
enable_testing()

# Everything except main.cpp, shared by the app and the tests.
add_library(capture_core STATIC
    src/CaptureWindow.cpp src/CaptureWindow.h
    src/HotkeyFilter.cpp src/HotkeyFilter.h
    src/MarkdownShortcuts.cpp src/MarkdownShortcuts.h
    src/NoteEdit.cpp src/NoteEdit.h
    src/Trials.cpp src/Trials.h
)
target_include_directories(capture_core PUBLIC src)
target_link_libraries(capture_core PUBLIC Qt6::Widgets user32)
target_compile_definitions(capture_core PUBLIC
    SPIKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" NOMINMAX WIN32_LEAN_AND_MEAN)

qt_add_executable(qt-capture WIN32 src/main.cpp)
target_link_libraries(qt-capture PRIVATE capture_core)

qt_add_executable(qt-capture-tests tests/tst_editor.cpp)
target_link_libraries(qt-capture-tests PRIVATE capture_core Qt6::Test)
add_test(NAME editor COMMAND qt-capture-tests)
set_tests_properties(editor PROPERTIES
    # windeployqt puts only the "windows" platform plugin next to the exes.
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
