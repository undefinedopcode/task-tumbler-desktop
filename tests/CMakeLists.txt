find_package(Qt6 REQUIRED COMPONENTS Test)

function(tumbler_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE tumbler-core Qt6::Test)
    target_compile_definitions(${name} PRIVATE TUMBLER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

tumbler_add_test(tst_recurrence)
tumbler_add_test(tst_dependencies)
tumbler_add_test(tst_blocking)
tumbler_add_test(tst_categoryschedule)
tumbler_add_test(tst_timeparse)
tumbler_add_test(tst_database)
tumbler_add_test(tst_importer)
tumbler_add_test(tst_operations)
tumbler_add_test(tst_sync)
tumbler_add_test(tst_notes)
tumbler_add_test(tst_stats)
tumbler_add_test(tst_review)
tumbler_add_test(tst_planner)
tumbler_add_test(tst_capture)
tumbler_add_test(tst_reminders)
tumbler_add_test(tst_dayplanner)

# The app's controller layer, compiled straight in so it can be driven
# without QML.
if(BUILD_APP)
    set(APP_DIR ${PROJECT_SOURCE_DIR}/src/app)
    qt_add_executable(tst_appcontroller tst_appcontroller.cpp
        ${APP_DIR}/appcontroller.h ${APP_DIR}/appcontroller.cpp ${APP_DIR}/appcontroller_ai.cpp
        ${APP_DIR}/appcontroller_desktop.cpp ${APP_DIR}/appcontroller_external.cpp ${APP_DIR}/appcontroller_notes.cpp ${APP_DIR}/appcontroller_stats.cpp ${APP_DIR}/appcontroller_review.cpp ${APP_DIR}/appcontroller_calendar.cpp
        ${APP_DIR}/desktop/externalapi.h ${APP_DIR}/desktop/externalapi.cpp
        ${APP_DIR}/desktop/apps.h ${APP_DIR}/desktop/devicecalendars.h ${APP_DIR}/desktop/devicecalendars.cpp
        ${TUMBLER_CONTROLLER_PLATFORM_SOURCES}
        ${APP_DIR}/remote/remotehub.h ${APP_DIR}/remote/remotehub.cpp
        ${APP_DIR}/sync/synchub.h ${APP_DIR}/sync/synchub.cpp
        ${APP_DIR}/keystore.h ${APP_DIR}/keystore.cpp
        ${APP_DIR}/tasklistmodel.h ${APP_DIR}/tasklistmodel.cpp
        ${APP_DIR}/variants.h ${APP_DIR}/variants.cpp)
    target_include_directories(tst_appcontroller PRIVATE ${APP_DIR} ${APP_DIR}/remote ${APP_DIR}/sync)
    target_link_libraries(tst_appcontroller PRIVATE tumbler-core tumbler-ai tumbler-remote tumbler-sync tumbler-ipc Qt6Keychain::Qt6Keychain Qt6::Test Qt6::Gui ${TUMBLER_PLATFORM_LIBRARIES})

    target_compile_definitions(tst_appcontroller PRIVATE ${TUMBLER_PLATFORM_DEFINITIONS})

    qt_add_executable(tst_ipc tst_ipc.cpp)
    target_link_libraries(tst_ipc PRIVATE tumbler-ipc Qt6::Test)
    add_test(NAME tst_ipc COMMAND tst_ipc)

    qt_add_executable(tst_mcp tst_mcp.cpp)
    target_link_libraries(tst_mcp PRIVATE tumbler-mcp Qt6::Test)
    add_test(NAME tst_mcp COMMAND tst_mcp)
    target_compile_definitions(tst_appcontroller PRIVATE TUMBLER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME tst_appcontroller COMMAND tst_appcontroller)
endif()

qt_add_executable(tst_ai tst_ai.cpp)
target_link_libraries(tst_ai PRIVATE tumbler-ai Qt6::Test)
add_test(NAME tst_ai COMMAND tst_ai)

if(BUILD_APP)
    qt_add_executable(tst_remote tst_remote.cpp)
    target_link_libraries(tst_remote PRIVATE tumbler-remote Qt6::Test)
    add_test(NAME tst_remote COMMAND tst_remote)

    qt_add_executable(tst_syncclients tst_syncclients.cpp)
    target_link_libraries(tst_syncclients PRIVATE tumbler-sync Qt6::Test)
    add_test(NAME tst_syncclients COMMAND tst_syncclients)
endif()

# Against real models, on request only (see the file): never in CI.
qt_add_executable(tst_ai_live tst_ai_live.cpp)
target_link_libraries(tst_ai_live PRIVATE tumbler-ai Qt6::Test)
