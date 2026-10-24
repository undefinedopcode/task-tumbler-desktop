cmake_minimum_required(VERSION 3.20)
project(task-tumbler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT_MIN_VERSION 6.8.0)
set(KF_MIN_VERSION 6.8.0)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Sql)

qt_standard_project_setup(REQUIRES 6.8)

option(BUILD_TESTING "Build the test suite" ON)
option(BUILD_APP "Build the desktop app (needs KDE Frameworks)" ON)

add_subdirectory(src/core)
add_subdirectory(src/ai)
add_subdirectory(src/cli)

if(BUILD_APP)
    find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    include(KDEInstallDirs)

    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Gui Widgets Qml Quick QuickControls2 DBus)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
        Kirigami CoreAddons I18n DBusAddons Notifications StatusNotifierItem GlobalAccel)
    find_package(KF6KirigamiAddons 1.0 REQUIRED)
    find_package(Qt6Keychain REQUIRED)

    add_subdirectory(src/app)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
