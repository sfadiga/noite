# The noite version comes from git: a clean checkout of the tag v<project version> is exactly
# that version; anything else is "<project version>-dev+<sha>[.dirty]".

# Reads version.txt (one X.Y.Z line; a UTF-8 BOM, CR/LF and surrounding whitespace are tolerated)
# into out_var. Usable before project(). Anything else is a fatal error.
function(noite_read_version_file path out_var)
    set(text "")
    if(EXISTS "${path}")
        file(READ "${path}" text)
    endif()
    string(ASCII 239 187 191 bom)
    string(FIND "${text}" "${bom}" bom_at)
    if(bom_at EQUAL 0)
        string(SUBSTRING "${text}" 3 -1 text)
    endif()
    string(STRIP "${text}" text)
    if(NOT text MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "version.txt must contain X.Y.Z (got '${text}' from ${path})")
    endif()
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Turns a `git describe --tags --match "v*" --long --dirty --always` string into the version.
# `base` is always the project version; a tag only matters when it is exactly v<base> with no
# commits after it and no local changes.
function(noite_version_from_describe describe base out_var)
    set(version "${base}-dev")
    if(describe MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
        set(tag "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
        set(count "${CMAKE_MATCH_4}")
        set(sha "${CMAKE_MATCH_5}")
        set(dirty "${CMAKE_MATCH_6}")
        if(count EQUAL 0 AND dirty STREQUAL "" AND tag STREQUAL base)
            set(version "${base}")
        elseif(dirty STREQUAL "")
            set(version "${base}-dev+${sha}")
        else()
            set(version "${base}-dev+${sha}.dirty")
        endif()
    elseif(describe MATCHES "^[0-9a-f]+(-dirty)?$" OR describe MATCHES "-g([0-9a-f]+)(-dirty)?$")
        # A bare sha (no tags) or a describe of some other tag: only the sha counts.
        if(describe MATCHES "^([0-9a-f]+)(-dirty)?$")
            set(sha "${CMAKE_MATCH_1}")
            set(dirty "${CMAKE_MATCH_2}")
        else()
            string(REGEX MATCH "-g([0-9a-f]+)(-dirty)?$" ignored "${describe}")
            set(sha "${CMAKE_MATCH_1}")
            set(dirty "${CMAKE_MATCH_2}")
        endif()
        if(dirty STREQUAL "")
            set(version "${base}-dev+${sha}")
        else()
            set(version "${base}-dev+${sha}.dirty")
        endif()
    endif()
    set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# Configure-time driver: sets NOITE_VERSION and NOITE_VERSION_MAJOR/MINOR/PATCH in the caller's
# scope and re-runs configure when HEAD (or the ref it points to) changes.
function(noite_configure_version)
    set(describe "")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v*" --long --dirty --always
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE describe
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE rc)
        if(NOT rc EQUAL 0)
            set(describe "")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE git_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE git_dir_rc)
        if(git_dir_rc EQUAL 0 AND EXISTS "${git_dir}/HEAD")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/HEAD")
            file(READ "${git_dir}/HEAD" head)
            if(head MATCHES "^ref: ([^\r\n]+)")
                if(EXISTS "${git_dir}/${CMAKE_MATCH_1}")
                    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/${CMAKE_MATCH_1}")
                endif()
            endif()
        endif()
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/version.txt")

    noite_version_from_describe("${describe}" "${PROJECT_VERSION}" version)
    set(NOITE_VERSION "${version}" PARENT_SCOPE)
    set(NOITE_VERSION_BASE "${PROJECT_VERSION}" PARENT_SCOPE)
    set(NOITE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}" PARENT_SCOPE)
    set(NOITE_VERSION_MINOR "${PROJECT_VERSION_MINOR}" PARENT_SCOPE)
    set(NOITE_VERSION_PATCH "${PROJECT_VERSION_PATCH}" PARENT_SCOPE)
    message(STATUS "noite version: ${version}")
endfunction()
