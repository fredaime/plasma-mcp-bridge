# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

# 0.1.90 = pre-release of 0.2.0 (see CHANGELOG.md).
project(plasma-mcp-bridge VERSION 0.1.90 LANGUAGES CXX)

set(QT_MIN_VERSION 6.4.0)
set(ECM_MIN_VERSION 5.100.0)

find_package(ECM ${ECM_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core DBus)

# Same deprecation warnings on the CI's Qt 6.4 and on newer local Qt: APIs
# deprecated after 6.4 do not warn (QT_WARN_DEPRECATED_UP_TO is the 6.5+ name,
# QT_DEPRECATED_WARNINGS_SINCE the 6.4 one).
add_compile_definitions(QT_WARN_DEPRECATED_UP_TO=0x060400 QT_DEPRECATED_WARNINGS_SINCE=0x060400)

add_subdirectory(src)
add_subdirectory(data)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
