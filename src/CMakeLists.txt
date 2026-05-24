# SPDX-License-Identifier: MIT

# --- Core shared library ------------------------------------------------------
# All the protocol/dbus/tools logic lives here. Both the plasma-mcp-bridge
# executable and any out-of-tree plugin link against this library, so a
# plugin's call back into ToolRegistry::add (etc.) is resolved at runtime by
# the normal dynamic linker rather than relying on -rdynamic / exported
# executable symbols.
add_library(plasma-mcp-bridge-core SHARED
    mcp/jsonrpc.cpp
    mcp/stdiotransport.cpp
    mcp/toolregistry.cpp
    mcp/server.cpp
    dbus/dbusbridge.cpp
    core/pluginloader.cpp
    core/skillemitter.cpp
    backends/dbusbackend.cpp
    backends/notificationbackend.cpp
    tools/dbustools.cpp
    tools/notifytool.cpp
)

target_link_libraries(plasma-mcp-bridge-core
    PUBLIC Qt6::Core Qt6::DBus)

# Stamped into both the library (server.cpp emits it via serverInfo.version)
# and propagated to the executable (main.cpp echoes it in startup logs).
target_compile_definitions(plasma-mcp-bridge-core
    PUBLIC PLASMA_MCP_BRIDGE_VERSION="${PROJECT_VERSION}")

target_include_directories(plasma-mcp-bridge-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge>
)

set_target_properties(plasma-mcp-bridge-core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME Core)

# --- Executable ---------------------------------------------------------------
add_executable(plasma-mcp-bridge main.cpp)
target_link_libraries(plasma-mcp-bridge PRIVATE plasma-mcp-bridge-core)

install(TARGETS plasma-mcp-bridge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# --- Plugin ABI ---------------------------------------------------------------
# Out-of-tree plugins consume this via
#   find_package(PlasmaMcpBridge REQUIRED)
#   target_link_libraries(my_plugin PRIVATE PlasmaMcpBridge::PluginInterface)
# Linking to PluginInterface pulls in plasma-mcp-bridge-core (and through it
# Qt6::Core + Qt6::DBus), so plugin code can call ToolRegistry::add,
# DBusBridge::callMethod, etc.
add_library(PlasmaMcpBridgePluginInterface INTERFACE)
add_library(PlasmaMcpBridge::PluginInterface ALIAS PlasmaMcpBridgePluginInterface)

set_target_properties(PlasmaMcpBridgePluginInterface PROPERTIES
    EXPORT_NAME PluginInterface)

target_link_libraries(PlasmaMcpBridgePluginInterface
    INTERFACE plasma-mcp-bridge-core)

# --- Install + export the ABI -------------------------------------------------
install(FILES core/backend.h core/plugin.h core/skillemitter.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge/core)
install(FILES mcp/tool.h mcp/toolregistry.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge/mcp)
install(FILES dbus/dbusbridge.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge/dbus)

install(TARGETS plasma-mcp-bridge-core PlasmaMcpBridgePluginInterface
    EXPORT PlasmaMcpBridgeTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})

install(EXPORT PlasmaMcpBridgeTargets
    NAMESPACE PlasmaMcpBridge::
    FILE PlasmaMcpBridgeTargets.cmake
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PlasmaMcpBridge)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/PlasmaMcpBridgeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/PlasmaMcpBridgeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/PlasmaMcpBridgeConfig.cmake
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PlasmaMcpBridge)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/PlasmaMcpBridgeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/PlasmaMcpBridgeConfigVersion.cmake
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PlasmaMcpBridge)
