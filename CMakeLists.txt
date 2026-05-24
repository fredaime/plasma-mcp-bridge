# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(plasma-mcp-bridge VERSION 0.1.0 LANGUAGES CXX)

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

add_subdirectory(src)
add_subdirectory(data)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
