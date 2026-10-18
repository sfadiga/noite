cmake_minimum_required(VERSION 3.21)
project(noite LANGUAGES C CXX)

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

add_subdirectory(third_party/sqlite)
add_subdirectory(core)
add_subdirectory(app)
