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
    remote/remotehub.h remote/remotehub.cpp
    sync/synchub.h sync/synchub.cpp
)

# What differs between desktops: TUMBLER_PLATFORM_SOURCES and _LIBRARIES
# come from the top-level CMakeLists.txt.
target_sources(task-tumbler PRIVATE ${TUMBLER_PLATFORM_SOURCES})

# Must precede qt_add_qml_module to take effect.
set_source_files_properties(qml/components/Format.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(task-tumbler
    URI org.kde.tasktumbler
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/QuickCaptureWindow.qml
        qml/pages/TodayPage.qml
        qml/pages/TasksPage.qml
        qml/pages/TaskDetailPage.qml
        qml/pages/SchedulePage.qml
        qml/pages/CategoriesPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/OnboardingPage.qml
        qml/pages/PlanPage.qml
        qml/pages/EnergyPage.qml
        qml/pages/FocusPage.qml
        qml/pages/NotesPage.qml
        qml/pages/NoteEditorPage.qml
        qml/pages/StatsPage.qml
        qml/pages/CloseOutPage.qml
        qml/pages/WeekReviewPage.qml
        qml/components/Chip.qml
        qml/components/BarChart.qml
        qml/components/SectionTitle.qml
        qml/components/AgendaRow.qml
        qml/components/WorkingCard.qml
        qml/components/ReadyCard.qml
        qml/components/TaskRow.qml
        qml/components/CategoryDialog.qml
        qml/components/EventDialog.qml
        qml/components/FeedDialog.qml
        qml/components/EnergyCheckIn.qml
        qml/components/LauncherDialog.qml
        qml/components/Format.qml
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
    Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::Notifications KF6::StatusNotifierItem
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
