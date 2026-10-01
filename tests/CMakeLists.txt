# SPDX-License-Identifier: MIT
# Python tests on a private D-Bus. Missing test dependencies only disable the
# tests (WARNING), so consumers that configure the core without them still build.
find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python 3 not found: plasma-mcp-bridge tests disabled")
    return()
endif()

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import dbus, dbus.service, dbus.mainloop.glib; from gi.repository import GLib"
    RESULT_VARIABLE _plasma_mcp_test_deps OUTPUT_QUIET ERROR_QUIET)
if(NOT _plasma_mcp_test_deps EQUAL 0)
    message(WARNING "python3-dbus or python3-gi not found: plasma-mcp-bridge tests disabled")
    return()
endif()

find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
if(NOT DBUS_RUN_SESSION_EXECUTABLE)
    message(WARNING "dbus-run-session not found: plasma-mcp-bridge tests disabled")
    return()
endif()

# One module per line: each PR adds its own line.
set(_tests
    test_smoke
    test_harness
    test_demarshall
    test_call_path
    test_coercion
    test_policy
)

foreach(_test IN LISTS _tests)
    add_test(NAME ${_test}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_with_bus.sh
                ${Python3_EXECUTABLE} -m unittest -v ${_test}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${_test} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "PLASMA_MCP_BRIDGE=$<TARGET_FILE:plasma-mcp-bridge>;PYTHONDONTWRITEBYTECODE=1;PLASMA_MCP_BUILD_DIR=${PROJECT_BINARY_DIR};CMAKE_COMMAND=${CMAKE_COMMAND}")
endforeach()
