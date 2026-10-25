# tumbler-sync: clients for the services tasks and events are pulled from
# (Linear, calendar feeds). They fetch and translate; what changes in the
# database is decided by core/sync.
find_package(Qt6 REQUIRED COMPONENTS Network)
find_package(KF6CalendarCore REQUIRED)

qt_add_library(tumbler-sync STATIC
    linear.h linear.cpp
    icsfeed.h icsfeed.cpp
)
target_include_directories(tumbler-sync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tumbler-sync PUBLIC tumbler-core Qt6::Network KF6::CalendarCore)
