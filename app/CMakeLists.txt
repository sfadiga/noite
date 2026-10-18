# Everything except main.cpp, shared by the noite executable and app-tests.
add_library(noite-app STATIC
    src/App.h src/App.cpp
    src/AppOptions.h src/AppOptions.cpp
    src/platform/Hotkey.h src/platform/Hotkey.cpp
    src/platform/HotkeyManager.h src/platform/HotkeyManager.cpp
    src/platform/NativeHost.h src/platform/NativeHost.cpp
    src/editor/MarkdownShortcuts.h src/editor/MarkdownShortcuts.cpp
    src/editor/NoteEdit.h src/editor/NoteEdit.cpp
    src/editor/AttachmentStore.h src/editor/AttachmentStore.cpp
    src/editor/OpenFolder.h src/editor/OpenFolder.cpp
    src/editor/PasteCleaner.h src/editor/PasteCleaner.cpp
    src/editor/DateTokens.h src/editor/DateTokens.cpp
    src/capture/CaptureController.h src/capture/CaptureController.cpp
    src/capture/CaptureWindow.h src/capture/CaptureWindow.cpp
    src/capture/LatencyProbe.h src/capture/LatencyProbe.cpp
    src/capture/KeepWarm.h src/capture/KeepWarm.cpp
    src/capture/NoteFooter.h src/capture/NoteFooter.cpp
    src/capture/TagField.h src/capture/TagField.cpp
    src/capture/NoteFooterBar.h src/capture/NoteFooterBar.cpp
    src/board/BoardModel.h src/board/BoardModel.cpp
    src/board/CardDelegate.h src/board/CardDelegate.cpp
    src/board/BoardWindow.h src/board/BoardWindow.cpp
    src/board/UndoLog.h src/board/UndoLog.cpp
    src/board/NoteOverlay.h src/board/NoteOverlay.cpp
    src/board/TabStrip.h src/board/TabStrip.cpp
    src/board/FlowLayout.h src/board/FlowLayout.cpp
    src/board/ChipBar.h src/board/ChipBar.cpp
    src/Theme.h src/Theme.cpp
    src/DragMover.h src/DragMover.cpp
    src/radar/ReviewScheduler.h src/radar/ReviewScheduler.cpp
    src/radar/ReviewWindow.h src/radar/ReviewWindow.cpp
    src/platform/Autostart.h src/platform/Autostart.cpp
    src/log/Log.h src/log/Log.cpp
    src/platform/CrashHandler.h src/platform/CrashHandler.cpp
    src/platform/DriveFolder.h src/platform/DriveFolder.cpp
    src/jobs/BackupFolderDialog.h src/jobs/BackupFolderDialog.cpp
    src/jobs/BackupRunner.h src/jobs/BackupRunner.cpp
    src/jobs/JobScheduler.h src/jobs/JobScheduler.cpp
)
target_include_directories(noite-app PUBLIC src)
target_link_libraries(noite-app PUBLIC noite-core Qt6::Widgets user32 wtsapi32 dbghelp)
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
    tests/tst_hotkey_manager.cpp
    tests/tst_native_host.cpp
    tests/tst_editor.cpp tests/fake_attachments.h tests/widget_helpers.h
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
    tests/tst_board_actions.cpp
    tests/tst_note_overlay.cpp
    tests/tst_theme.cpp
    tests/tst_chip_bar.cpp
    tests/tst_tag_field.cpp
    tests/tst_review_scheduler.cpp
    tests/tst_autostart.cpp
    tests/tst_review_window.cpp
    tests/tst_log.cpp
    tests/tst_crash.cpp
    tests/tst_drive_folder.cpp
    tests/tst_backup_folder_dialog.cpp
    tests/tst_backup_runner.cpp
    tests/tst_job_scheduler.cpp
    tests/tst_seed.cpp bench/Seed.h bench/Seed.cpp
)
# Reuses core's test registry and helpers.
target_include_directories(app-tests PRIVATE tests bench ${PROJECT_SOURCE_DIR}/core/tests)
target_link_libraries(app-tests PRIVATE noite-app sqlite3 Qt6::Test)
add_test(NAME app COMMAND app-tests)

# Crashes on purpose for tst_crash.cpp; lands next to app-tests.exe, never shipped.
qt_add_executable(noite_crash_helper WIN32 tests/crash_helper.cpp)
target_link_libraries(noite_crash_helper PRIVATE noite-app)
add_dependencies(app-tests noite_crash_helper)
