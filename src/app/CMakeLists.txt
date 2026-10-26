qt_add_executable(task-tumbler
    main.cpp
    appcontroller.h appcontroller.cpp appcontroller_ai.cpp appcontroller_desktop.cpp appcontroller_external.cpp appcontroller_notes.cpp appcontroller_stats.cpp
    keystore.h keystore.cpp
    tasklistmodel.h tasklistmodel.cpp
    variants.h variants.cpp
    desktop/reminders.h desktop/reminders.cpp
    desktop/tray.h desktop/tray.cpp
    desktop/autostart.h desktop/autostart.cpp
    desktop/launcher.h desktop/launcher.cpp
    desktop/idlewatch.h desktop/idlewatch.cpp
    desktop/focusguard.h desktop/focusguard.cpp
    desktop/dbusapi.h desktop/dbusapi.cpp
    desktop/krunner.h desktop/krunner.cpp
    remote/remotehub.h remote/remotehub.cpp
    sync/synchub.h sync/synchub.cpp
)

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
    tumbler-remote
    tumbler-sync
    Qt6Keychain::Qt6Keychain
    KF6::KIOGui KF6::Service KF6::IdleTime
    Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::DBusAddons KF6::Notifications KF6::StatusNotifierItem KF6::GlobalAccel
)

# Running from the build tree: KNotification looks for the notifyrc under
# $XDG_DATA_DIRS/knotifications6, so stage a copy and point the app at it.
configure_file(${PROJECT_SOURCE_DIR}/resources/desktop/tasktumbler.notifyrc
               ${CMAKE_BINARY_DIR}/share/knotifications6/tasktumbler.notifyrc COPYONLY)
target_compile_definitions(task-tumbler PRIVATE TUMBLER_DEV_DATA_DIR="${CMAKE_BINARY_DIR}/share")

install(TARGETS task-tumbler ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${PROJECT_SOURCE_DIR}/resources/desktop/org.kde.tasktumbler.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${PROJECT_SOURCE_DIR}/resources/desktop/tasktumbler.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/org.kde.tasktumbler.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
