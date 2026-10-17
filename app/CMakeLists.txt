# Everything except main.cpp, shared by the noite executable and app-tests.
add_library(noite-app STATIC
    src/App.h src/App.cpp
    src/AppOptions.h src/AppOptions.cpp
    src/platform/Hotkey.h src/platform/Hotkey.cpp
    src/platform/NativeHost.h src/platform/NativeHost.cpp
    src/editor/MarkdownShortcuts.h src/editor/MarkdownShortcuts.cpp
    src/editor/NoteEdit.h src/editor/NoteEdit.cpp
    src/editor/AttachmentStore.h src/editor/AttachmentStore.cpp
    src/editor/PasteCleaner.h src/editor/PasteCleaner.cpp
    src/editor/DateTokens.h src/editor/DateTokens.cpp
    src/capture/CaptureController.h src/capture/CaptureController.cpp
    src/capture/CaptureWindow.h src/capture/CaptureWindow.cpp
    src/capture/LatencyProbe.h src/capture/LatencyProbe.cpp
    src/capture/KeepWarm.h src/capture/KeepWarm.cpp
    src/capture/NoteFooter.h src/capture/NoteFooter.cpp
    src/board/BoardModel.h src/board/BoardModel.cpp
    src/board/CardDelegate.h src/board/CardDelegate.cpp
    src/board/BoardWindow.h src/board/BoardWindow.cpp
)
target_include_directories(noite-app PUBLIC src)
target_link_libraries(noite-app PUBLIC noite-core Qt6::Widgets user32)
target_compile_definitions(noite-app PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

qt_add_executable(noite WIN32 src/main.cpp)
target_link_libraries(noite PRIVATE noite-app)

# Board performance gate (D5): not shipped, run by hand on a Release build.
qt_add_executable(board-bench bench/board_bench.cpp bench/Seed.h bench/Seed.cpp)
target_include_directories(board-bench PRIVATE bench)
target_link_libraries(board-bench PRIVATE noite-app psapi)

qt_add_executable(app-tests
    tests/main.cpp
    tests/tst_smoke.cpp
    tests/tst_hotkey.cpp
    tests/tst_native_host.cpp
    tests/tst_editor.cpp tests/fake_attachments.h
    tests/tst_paste.cpp
    tests/tst_dates.cpp
    tests/tst_capture_controller.cpp
    tests/tst_capture_window.cpp
    tests/tst_app.cpp
    tests/tst_latency_probe.cpp
    tests/tst_keep_warm.cpp
    tests/tst_board_model.cpp
    tests/tst_card_delegate.cpp
    tests/tst_board_window.cpp
    tests/tst_seed.cpp bench/Seed.h bench/Seed.cpp
)
# Reuses core's test registry and helpers.
target_include_directories(app-tests PRIVATE tests bench ${PROJECT_SOURCE_DIR}/core/tests)
target_link_libraries(app-tests PRIVATE noite-app Qt6::Test)
add_test(NAME app COMMAND app-tests)
