cmake_minimum_required(VERSION 3.21)

# version.txt is the version source; it must be read before project().
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(NoiteVersion)
noite_read_version_file("${CMAKE_CURRENT_LIST_DIR}/version.txt" NOITE_BASE)
project(noite VERSION ${NOITE_BASE} LANGUAGES C CXX)

noite_configure_version() # NOITE_VERSION from git describe

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources are UTF-8; without this MSVC reads them in the system codepage. Release builds get
# PDBs for the crash dumps, same optimisation (D29); tools/deploy.ps1 ships only the exe.
if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
    add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/Zi>")
    add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Widgets Test)
qt_standard_project_setup()
enable_testing()
add_test(NAME version COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/cmake/tests/version_test.cmake)

add_subdirectory(third_party/sqlite)
add_subdirectory(core)
add_subdirectory(app)
