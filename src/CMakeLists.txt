# SPDX-License-Identifier: MIT
add_executable(plasma-mcp-bridge
    main.cpp
    mcp/jsonrpc.cpp
    mcp/stdiotransport.cpp
    mcp/toolregistry.cpp
    mcp/server.cpp
    dbus/dbusbridge.cpp
    tools/dbustools.cpp
    tools/notifytool.cpp
)

target_include_directories(plasma-mcp-bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(plasma-mcp-bridge
    PRIVATE PLASMA_MCP_BRIDGE_VERSION="${PROJECT_VERSION}")

target_link_libraries(plasma-mcp-bridge
    Qt6::Core
    Qt6::DBus
)

install(TARGETS plasma-mcp-bridge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
