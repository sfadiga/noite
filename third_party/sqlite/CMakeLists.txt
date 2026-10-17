# Vendored SQLite amalgamation, see VERSION.
add_library(sqlite3 STATIC sqlite3.c sqlite3.h)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_DQS=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
    SQLITE_USE_URI=0)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /w)
endif()
