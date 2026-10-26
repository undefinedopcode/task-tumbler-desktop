# tumbler-ipc: how the running app is reached from outside it (the MCP
# server, a second launch) on every platform: JSON lines over a local socket.
find_package(Qt6 REQUIRED COMPONENTS Network)

qt_add_library(tumbler-ipc STATIC
    ipc.h ipc.cpp
)
target_include_directories(tumbler-ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tumbler-ipc PUBLIC Qt6::Core Qt6::Network)
