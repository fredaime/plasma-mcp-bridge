# SPDX-License-Identifier: MIT
configure_file(org.kde.plasma.mcpbridge.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.kde.plasma.mcpbridge.service @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.plasma.mcpbridge.service
    DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
