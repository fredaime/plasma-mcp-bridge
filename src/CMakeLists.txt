# SPDX-License-Identifier: MIT

# Source files split by layer (mcp/ protocol, dbus/ bridge, core/ plugin ABI,
# backends/ built-in capabilities, tools/ concrete MCP tools).
add_executable(plasma-mcp-bridge
    main.cpp
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

target_include_directories(plasma-mcp-bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(plasma-mcp-bridge
    PRIVATE PLASMA_MCP_BRIDGE_VERSION="${PROJECT_VERSION}")

target_link_libraries(plasma-mcp-bridge
    Qt6::Core
    Qt6::DBus
)

install(TARGETS plasma-mcp-bridge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# --- Plugin ABI ----------------------------------------------------------------
# An INTERFACE target out-of-tree plugins consume via
#   find_package(PlasmaMcpBridge REQUIRED)
#   target_link_libraries(my_plugin PRIVATE PlasmaMcpBridge::PluginInterface)
# It carries the include path to the installed public headers and the Qt6
# linkage every plugin needs.
add_library(PlasmaMcpBridgePluginInterface INTERFACE)
add_library(PlasmaMcpBridge::PluginInterface ALIAS PlasmaMcpBridgePluginInterface)

# The exported target gets EXPORT_NAME stripped of the package prefix so
# downstream consumers see PlasmaMcpBridge::PluginInterface (matching the
# in-tree alias) rather than PlasmaMcpBridge::PlasmaMcpBridgePluginInterface.
set_target_properties(PlasmaMcpBridgePluginInterface PROPERTIES
    EXPORT_NAME PluginInterface)

target_link_libraries(PlasmaMcpBridgePluginInterface
    INTERFACE Qt6::Core Qt6::DBus)

target_include_directories(PlasmaMcpBridgePluginInterface INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge>
)

install(FILES core/backend.h core/plugin.h core/skillemitter.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge/core)
install(FILES mcp/tool.h mcp/toolregistry.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge/mcp)
install(FILES dbus/dbusbridge.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/plasma-mcp-bridge/dbus)

install(TARGETS PlasmaMcpBridgePluginInterface
    EXPORT PlasmaMcpBridgeTargets)

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
