cmake_minimum_required(VERSION 3.20)
project(task-tumbler VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX) # EventKit (src/app/desktop/*_mac.mm)
endif()

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
if(BUILD_APP)
    add_subdirectory(src/ipc)
    add_subdirectory(src/mcp)
    add_subdirectory(src/remote) # OpenSSL
    add_subdirectory(src/sync)   # KCalendarCore
endif()
add_subdirectory(src/cli)

if(BUILD_APP)
    find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    include(KDEInstallDirs)

    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Gui Widgets Qml Quick QuickControls2 WebEngineWidgets WebChannel)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Kirigami CoreAddons I18n Notifications StatusNotifierItem)
    find_package(KF6KirigamiAddons 1.0 REQUIRED)
    find_package(Qt6Keychain REQUIRED)

    # The few things each desktop does its own way (src/app/desktop/*_linux.cpp
    # and *_mac.cpp): opening apps, idle time, the global shortcut, login items.
    set(APP_DESKTOP ${PROJECT_SOURCE_DIR}/src/app/desktop)
    if(APPLE)
        # What the controller itself needs (the tests build these too)...
        set(TUMBLER_CONTROLLER_PLATFORM_SOURCES ${APP_DESKTOP}/apps_mac.cpp ${APP_DESKTOP}/devicecalendars_mac.mm)
        set_source_files_properties(${APP_DESKTOP}/devicecalendars_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        # ...and the rest of the app.
        set(TUMBLER_PLATFORM_SOURCES ${TUMBLER_CONTROLLER_PLATFORM_SOURCES}
            ${APP_DESKTOP}/presence_mac.cpp ${APP_DESKTOP}/hotkey_mac.cpp
            ${APP_DESKTOP}/idlewatch_mac.cpp ${APP_DESKTOP}/autostart_mac.cpp)
        # The Breeze icons, built into KIconThemes, recoloured by its icon engine.
        find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS IconThemes)
        set(TUMBLER_PLATFORM_LIBRARIES KF6::IconThemes "-framework Carbon" "-framework CoreGraphics" "-framework CoreFoundation"
            "-framework EventKit" "-framework AppKit")
        set(TUMBLER_PLATFORM_DEFINITIONS "")
    else()
        # Plus the session bus: scripts, KRunner and Do Not Disturb.
        find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS DBus)
        find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS GlobalAccel KIO Service IdleTime)
        set(TUMBLER_CONTROLLER_PLATFORM_SOURCES ${APP_DESKTOP}/apps_linux.cpp ${APP_DESKTOP}/devicecalendars_linux.cpp)
        set(TUMBLER_PLATFORM_SOURCES ${TUMBLER_CONTROLLER_PLATFORM_SOURCES}
            ${APP_DESKTOP}/presence_linux.cpp ${APP_DESKTOP}/hotkey_linux.cpp
            ${APP_DESKTOP}/idlewatch_linux.cpp ${APP_DESKTOP}/autostart_linux.cpp
            ${APP_DESKTOP}/focusguard.h ${APP_DESKTOP}/focusguard.cpp ${APP_DESKTOP}/krunner.h ${APP_DESKTOP}/krunner.cpp)
        set(TUMBLER_PLATFORM_LIBRARIES Qt6::DBus KF6::KIOGui KF6::Service KF6::IdleTime KF6::GlobalAccel)
        set(TUMBLER_PLATFORM_DEFINITIONS TUMBLER_HAVE_DBUS)
    endif()

    add_subdirectory(src/app)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
