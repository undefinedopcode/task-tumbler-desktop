# tumbler-remote: the phone companion's server side. The HTTP server and API
# (httpserver.*), Web Push encryption and signing (webpush.*), and the web
# app itself (web/, embedded as :/remote/web). No GUI.
find_package(Qt6 REQUIRED COMPONENTS Network)
find_package(OpenSSL 3.0 REQUIRED)

qt_add_library(tumbler-remote STATIC
    httpserver.h httpserver.cpp
    webpush.h webpush.cpp
)

file(GLOB REMOTE_WEB_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS web/*)
qt_add_resources(tumbler-remote "remote_web"
    PREFIX "/remote"
    FILES ${REMOTE_WEB_FILES}
)

target_include_directories(tumbler-remote PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tumbler-remote PUBLIC Qt6::Core Qt6::Network OpenSSL::Crypto)
