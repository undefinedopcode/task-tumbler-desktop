# libtumbler-core: domain rules and storage. No GUI dependencies, so every
# rule here can be unit-tested and reused by the app, the CLI and the hub.
qt_add_library(tumbler-core STATIC
    types.h types.cpp
    recurrence.h recurrence.cpp
    dependencygraph.h dependencygraph.cpp
    blocking.h blocking.cpp
    categoryschedule.h categoryschedule.cpp
    timeparse.h timeparse.cpp
    database.h database.cpp
    importer.h importer.cpp
    entities.h
    queries.h queries.cpp
    mutation.h mutation.cpp
    operations.h operations.cpp
    planner.h planner.cpp
    today.h today.cpp
    captureparser.h captureparser.cpp
    reminderplan.h reminderplan.cpp
    dayplanner.h dayplanner.cpp
    sync.h sync.cpp
    notes.h notes.cpp
)

qt_add_resources(tumbler-core "migrations"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        ${PROJECT_SOURCE_DIR}/resources/migrations/001_initial.sql
        ${PROJECT_SOURCE_DIR}/resources/migrations/002_sync_links.sql
)

target_include_directories(tumbler-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tumbler-core PUBLIC Qt6::Core Qt6::Sql)
