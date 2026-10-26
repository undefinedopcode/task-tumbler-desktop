# tumbler-mcp: the Model Context Protocol server behind `task-tumbler --mcp`.
qt_add_library(tumbler-mcp STATIC
    mcpserver.h mcpserver.cpp
    socketbackend.h socketbackend.cpp
)
target_include_directories(tumbler-mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tumbler-mcp PUBLIC Qt6::Core PRIVATE tumbler-ipc)
