# Everything except main.cpp, shared by the noite executable and app-tests.
add_library(noite-app STATIC
    src/App.h src/App.cpp
    src/AppOptions.h
    src/platform/Hotkey.h src/platform/Hotkey.cpp
    src/platform/NativeHost.h src/platform/NativeHost.cpp
    src/editor/MarkdownShortcuts.h src/editor/MarkdownShortcuts.cpp
    src/editor/NoteEdit.h src/editor/NoteEdit.cpp
    src/editor/AttachmentStore.h src/editor/AttachmentStore.cpp
    src/editor/PasteCleaner.h src/editor/PasteCleaner.cpp
)
target_include_directories(noite-app PUBLIC src)
target_link_libraries(noite-app PUBLIC noite-core Qt6::Widgets user32)
target_compile_definitions(noite-app PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

qt_add_executable(noite WIN32 src/main.cpp)
target_link_libraries(noite PRIVATE noite-app)

qt_add_executable(app-tests
    tests/main.cpp
    tests/tst_smoke.cpp
    tests/tst_hotkey.cpp
    tests/tst_native_host.cpp
    tests/tst_editor.cpp tests/fake_attachments.h
    tests/tst_paste.cpp
)
# Reuses core's test registry and helpers.
target_include_directories(app-tests PRIVATE tests ${PROJECT_SOURCE_DIR}/core/tests)
target_link_libraries(app-tests PRIVATE noite-app Qt6::Test)
add_test(NAME app COMMAND app-tests)
