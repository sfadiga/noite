# Schema SQL stays a real .sql file; CMake embeds it as a raw string literal.
file(READ src/schema_v1.sql NOITE_SCHEMA_V1)
file(READ src/schema_v2.sql NOITE_SCHEMA_V2)
configure_file(src/schema.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/noite_schema.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/schema_v1.sql src/schema_v2.sql src/schema.h.in)

add_library(noite-core STATIC
    include/noite/error.h src/error.cpp
    include/noite/clock.h src/clock.cpp
    include/noite/data_dir.h src/data_dir.cpp
    include/noite/model.h src/model.cpp
    include/noite/content.h src/content.cpp
    include/noite/store.h src/store.cpp
    src/store_internal.h src/store_scratch.cpp
    src/store_lifecycle.cpp
    src/store_bundles.cpp
    src/store_board.cpp src/store_review.cpp
    src/store_attachments.cpp src/store_jobs.cpp
    include/noite/settings.h src/settings.cpp
    include/noite/backup.h src/backup.cpp
    src/db.h src/db.cpp
    src/migrate.h src/migrate.cpp src/schema_v1.sql src/schema_v2.sql
)
target_include_directories(noite-core
    PUBLIC include
    PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(noite-core PUBLIC Qt6::Core Qt6::Gui PRIVATE sqlite3)

qt_add_executable(core-tests
    tests/main.cpp tests/registry.h tests/helpers.h
    tests/tst_sqlite.cpp
    tests/tst_data_dir.cpp
    tests/tst_migrate.cpp
    tests/tst_store_open.cpp
    tests/tst_content.cpp
    tests/tst_scratch.cpp
    tests/tst_lifecycle.cpp
    tests/tst_bundles.cpp
    tests/tst_board.cpp
    tests/tst_review.cpp
    tests/tst_attachments.cpp
    tests/tst_settings.cpp
    tests/tst_backup.cpp
    tests/tst_backup_job.cpp
    tests/tst_job_state.cpp
    tests/tst_upgrade.cpp
)
target_include_directories(core-tests PRIVATE src tests)
target_link_libraries(core-tests PRIVATE noite-core sqlite3 Qt6::Test)
# The committed golden data folders (core/tests/golden/v<N>), opened by tst_upgrade and app-tests.
target_compile_definitions(core-tests PRIVATE NOITE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
add_test(NAME core COMMAND core-tests)

# Generates a golden data folder (tests/golden/make_golden.cpp). Built, never run by the tests.
qt_add_executable(noite_golden tests/golden/make_golden.cpp)
target_include_directories(noite_golden PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(noite_golden PRIVATE noite-core sqlite3 Qt6::Gui)
