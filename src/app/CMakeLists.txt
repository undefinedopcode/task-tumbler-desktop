qt_add_executable(task-tumbler
    main.cpp
    appcontroller.h appcontroller.cpp appcontroller_ai.cpp appcontroller_desktop.cpp appcontroller_external.cpp appcontroller_notes.cpp appcontroller_stats.cpp appcontroller_review.cpp
    keystore.h keystore.cpp
    tasklistmodel.h tasklistmodel.cpp
    variants.h variants.cpp
    desktop/reminders.h desktop/reminders.cpp
    desktop/tray.h desktop/tray.cpp
    desktop/autostart.h
    desktop/launcher.h desktop/launcher.cpp
    desktop/idlewatch.h desktop/idlewatch.cpp
    desktop/externalapi.h desktop/externalapi.cpp
    desktop/apps.h desktop/presence.h desktop/hotkey.h
    desktop/devicecalendars.h desktop/devicecalendars.cpp
    remote/remotehub.h remote/remotehub.cpp
    sync/synchub.h sync/synchub.cpp
    web/appscheme.h web/appscheme.cpp
    web/shellbridge.h web/shellbridge.cpp
    web/webshell.h web/webshell.cpp
)

# What differs between desktops: TUMBLER_PLATFORM_SOURCES and _LIBRARIES
# come from the top-level CMakeLists.txt.
target_sources(task-tumbler PRIVATE ${TUMBLER_PLATFORM_SOURCES})

# The React UI (webui/): npm builds it into one self-contained index.html,
# served from :/webui as tumbler://app/. TUMBLER_WEBUI_PREBUILT points at an
# index.html built elsewhere, for builds without Node.
set(TUMBLER_WEBUI_PREBUILT "" CACHE FILEPATH "A built webui index.html to use instead of running npm")
set(WEBUI_OUT ${CMAKE_CURRENT_BINARY_DIR}/webui)
if(TUMBLER_WEBUI_PREBUILT)
    configure_file(${TUMBLER_WEBUI_PREBUILT} ${WEBUI_OUT}/index.html COPYONLY)
else()
    set(WEBUI_DIR ${PROJECT_SOURCE_DIR}/webui)
    find_program(NPM_EXECUTABLE npm REQUIRED)
    file(GLOB_RECURSE WEBUI_SOURCES CONFIGURE_DEPENDS ${WEBUI_DIR}/src/*)
    # node_modules again only when the lock file changes.
    add_custom_command(
        OUTPUT ${WEBUI_OUT}/npm-ci.stamp
        COMMAND ${NPM_EXECUTABLE} ci --no-audit --no-fund --loglevel=error
        COMMAND ${CMAKE_COMMAND} -E touch ${WEBUI_OUT}/npm-ci.stamp
        DEPENDS ${WEBUI_DIR}/package.json ${WEBUI_DIR}/package-lock.json
        WORKING_DIRECTORY ${WEBUI_DIR}
        COMMENT "Installing the web UI's packages"
        VERBATIM)
    add_custom_command(
        OUTPUT ${WEBUI_OUT}/index.html
        COMMAND ${NPM_EXECUTABLE} run --silent build -- --outDir ${WEBUI_OUT} --emptyOutDir false --logLevel warn
        DEPENDS ${WEBUI_OUT}/npm-ci.stamp ${WEBUI_SOURCES} ${WEBUI_DIR}/index.html ${WEBUI_DIR}/vite.config.ts ${WEBUI_DIR}/tsconfig.json
        WORKING_DIRECTORY ${WEBUI_DIR}
        COMMENT "Building the web UI"
        VERBATIM)
endif()
qt_add_resources(task-tumbler "webui"
    PREFIX "/webui"
    BASE "${WEBUI_OUT}"
    FILES "${WEBUI_OUT}/index.html"
)

qt_add_resources(task-tumbler "icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES "${PROJECT_SOURCE_DIR}/resources/icons/org.kde.tasktumbler.svg"
)

target_include_directories(task-tumbler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/remote ${CMAKE_CURRENT_SOURCE_DIR}/sync)
target_link_libraries(task-tumbler PRIVATE
    tumbler-core
    tumbler-ai
    tumbler-mcp
    tumbler-ipc
    tumbler-remote
    tumbler-sync
    Qt6Keychain::Qt6Keychain
    Qt6::Gui Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel
    KF6::CoreAddons KF6::I18n KF6::Notifications KF6::StatusNotifierItem
    ${TUMBLER_PLATFORM_LIBRARIES}
)
target_compile_definitions(task-tumbler PRIVATE ${TUMBLER_PLATFORM_DEFINITIONS})

# Running from the build tree: KNotification looks for the notifyrc under
# $XDG_DATA_DIRS/knotifications6, so stage a copy and point the app at it.
configure_file(${PROJECT_SOURCE_DIR}/resources/desktop/tasktumbler.notifyrc
               ${CMAKE_BINARY_DIR}/share/knotifications6/tasktumbler.notifyrc COPYONLY)
target_compile_definitions(task-tumbler PRIVATE TUMBLER_DEV_DATA_DIR="${CMAKE_BINARY_DIR}/share")

if(APPLE)
    # A proper .app: Info.plist, the icon (an .icns made from the PNGs by
    # iconutil), and the notification config, which main.cpp hands to
    # KNotifications. .github/scripts/package-macos.sh bundles the libraries.
    include(ECMAddAppIcon)
    file(GLOB MAC_ICONS ${PROJECT_SOURCE_DIR}/resources/icons/macos/*-apps-tasktumbler.png)
    ecm_add_app_icon(MAC_ICON_SOURCES ICONS ${MAC_ICONS} OUTFILE_BASENAME task-tumbler)
    target_sources(task-tumbler PRIVATE ${MAC_ICON_SOURCES})
    qt_add_resources(task-tumbler "notifyrc"
        PREFIX "/notifyrc"
        BASE "${PROJECT_SOURCE_DIR}/resources/desktop"
        FILES "${PROJECT_SOURCE_DIR}/resources/desktop/tasktumbler.notifyrc"
    )
    set_target_properties(task-tumbler PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/resources/macos/Info.plist.in
    )
endif()

install(TARGETS task-tumbler ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${PROJECT_SOURCE_DIR}/resources/desktop/org.kde.tasktumbler.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${PROJECT_SOURCE_DIR}/resources/desktop/tasktumbler.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/org.kde.tasktumbler.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
