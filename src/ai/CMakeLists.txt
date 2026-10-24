# tumbler-ai: model providers (Claude, Ollama) and the AI features' prompts,
# schemas and answer handling. Network only, no GUI.
find_package(Qt6 REQUIRED COMPONENTS Network)

qt_add_library(tumbler-ai STATIC
    jsonschema.h jsonschema.cpp
    provider.h provider.cpp
    aifeatures.h aifeatures.cpp
)
target_include_directories(tumbler-ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tumbler-ai PUBLIC tumbler-core Qt6::Network)
