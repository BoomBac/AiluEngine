add_library(AiluBlenderLiveLink SHARED
    Src/BlenderLiveLinkPlugin.cpp
    Src/LiveLinkProtocol.cpp
    Src/LiveLinkServer.cpp
    Src/LiveLinkSession.cpp)

target_include_directories(AiluBlenderLiveLink PRIVATE
    "${CMAKE_SOURCE_DIR}/Engine/Inc"
    "${CMAKE_SOURCE_DIR}/Engine/Ext/rapidjson/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc")
target_compile_definitions(AiluBlenderLiveLink PRIVATE AILU_BUILD_PLUGIN UNICODE _UNICODE)
target_link_libraries(AiluBlenderLiveLink PRIVATE ws2_32)
target_link_libraries(AiluBlenderLiveLink PRIVATE Engine)
set_target_properties(AiluBlenderLiveLink PROPERTIES
    OUTPUT_NAME "AiluBlenderLiveLink"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<LOWER_CASE:$<CONFIG>>")

if (MSVC)
    target_compile_options(AiluBlenderLiveLink PRIVATE /utf-8)
endif()
