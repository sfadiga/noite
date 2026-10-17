add_library(noite-core STATIC
    include/noite/error.h src/error.cpp
    include/noite/clock.h src/clock.cpp
    include/noite/data_dir.h src/data_dir.cpp
)
target_include_directories(noite-core PUBLIC include PRIVATE src)
target_link_libraries(noite-core PUBLIC Qt6::Core Qt6::Gui PRIVATE sqlite3)

qt_add_executable(core-tests
    tests/main.cpp tests/registry.h tests/helpers.h
    tests/tst_sqlite.cpp
    tests/tst_data_dir.cpp
)
target_include_directories(core-tests PRIVATE src tests)
target_link_libraries(core-tests PRIVATE noite-core sqlite3 Qt6::Test)
add_test(NAME core COMMAND core-tests)
