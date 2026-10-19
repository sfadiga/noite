# Run with: cmake -P cmake/tests/version_test.cmake   (silent and exit 0 when everything matches)
include("${CMAKE_CURRENT_LIST_DIR}/../NoiteVersion.cmake")

function(expect describe expected)
    noite_version_from_describe("${describe}" "0.1.0" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "describe '${describe}': expected '${expected}', got '${actual}'")
    endif()
endfunction()

expect("v0.1.0-0-gabc1234" "0.1.0")
expect("v0.1.0-3-gabc1234" "0.1.0-dev+abc1234")
expect("v0.1.0-0-gabc1234-dirty" "0.1.0-dev+abc1234.dirty")
expect("v0.0.9-12-gdef5678" "0.1.0-dev+def5678")
expect("v0.0.9-0-gabc1234" "0.1.0-dev+abc1234")
expect("abc1234" "0.1.0-dev+abc1234")
expect("abc1234-dirty" "0.1.0-dev+abc1234.dirty")
expect("v0.2.0-rc1-0-gabc1234" "0.1.0-dev+abc1234")
expect("v1.0-0-gabc1234" "0.1.0-dev+abc1234")
expect("" "0.1.0-dev")

# noite_read_version_file: tolerated decorations are stripped, anything else is fatal.
set(tmp "${CMAKE_CURRENT_LIST_DIR}/../../build/version-test-tmp")
file(MAKE_DIRECTORY "${tmp}")

function(expect_file content expected)
    file(WRITE "${tmp}/version.txt" "${content}")
    noite_read_version_file("${tmp}/version.txt" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "version.txt '${content}': expected '${expected}', got '${actual}'")
    endif()
endfunction()

function(expect_bad content)
    file(WRITE "${tmp}/version.txt" "${content}")
    file(WRITE "${tmp}/read.cmake"
        "include(\"${CMAKE_CURRENT_LIST_DIR}/../NoiteVersion.cmake\")\n"
        "noite_read_version_file(\"${tmp}/version.txt\" v)\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -P "${tmp}/read.cmake"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 0 OR NOT "${out}${err}" MATCHES "version.txt must contain X.Y.Z")
        message(FATAL_ERROR "version.txt '${content}' should fail with the X.Y.Z message (rc=${rc}): ${out}${err}")
    endif()
endfunction()

expect_file("1.0.0" "1.0.0")
expect_file("1.0.0\n" "1.0.0")
string(ASCII 239 187 191 bom) # UTF-8 byte order mark; CMake strings have no \x escape
expect_file("${bom}1.0.0\r\n" "1.0.0")
expect_file(" 1.0.0 \n" "1.0.0")
expect_bad("1.0")
expect_bad("v1.0.0")
expect_bad("")
file(REMOVE_RECURSE "${tmp}")
