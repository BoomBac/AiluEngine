project (AiluPlayer)
set(PLAYER_ROOT_PATH ${CMAKE_SOURCE_DIR}/Player)
set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(PLAYER_RUNTIME_OUTPUT_DIR ${AiluBuildPath}/out/player/bin/x64)
set(PLAYER_PACKAGE_DIR "${AiluBuildPath}/out/player/package/x64/$<LOWER_CASE:$<CONFIG>>")
set(PLAYER_RUNTIME_RESOURCE_ROOT "../../../package/x64/$<LOWER_CASE:$<CONFIG>>/Engine/Res")
set(PLAYER_RUNTIME_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/player.runtime.$<LOWER_CASE:$<CONFIG>>.json")
set(PLAYER_DXC_DLL_DIR ${ENGINE_ROOT_PATH}/Ext/dxc/bin/x64)
set(PLAYER_PIX_DLL ${ENGINE_ROOT_PATH}/Ext/pix/WinPixEventRuntime.dll)
set(PLAYER_FBX_DLL "$<$<CONFIG:Debug>:${ENGINE_ROOT_PATH}/Ext/fbxsdk/lib/vs2019/x64/debug/libfbxsdk.dll>$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:${ENGINE_ROOT_PATH}/Ext/fbxsdk/lib/vs2019/x64/release/libfbxsdk.dll>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${AiluBuildPath}/out/player/bin/x64)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${AiluBuildPath}/out/player/bin/x64/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${AiluBuildPath}/out/player/bin/x64/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${AiluBuildPath}/out/player/bin/x64/relwithdebinfo)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${AiluBuildPath}/out/player/bin/x64/minsizerel)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(add_source_files_recursive dir)
    file(GLOB_RECURSE SOURCES RELATIVE ${dir} ${dir}/*.cpp ${dir}/*.h)
    foreach(source ${SOURCES})
        if(IS_DIRECTORY ${dir}/${source})
            add_source_files_recursive(${dir}/${source})
        else()
            list(APPEND PROJECT_SOURCES ${dir}/${source})
        endif()
    endforeach()
    set(PROJECT_SOURCES ${PROJECT_SOURCES} PARENT_SCOPE)
endfunction()

add_source_files_recursive(${PLAYER_ROOT_PATH}/Src)
add_source_files_recursive(${PLAYER_ROOT_PATH}/Inc)

add_executable(Player WIN32 ${PROJECT_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${PROJECT_SOURCES})

target_include_directories(Player PUBLIC ${PLAYER_ROOT_PATH}/)
target_include_directories(Player PUBLIC ${PLAYER_ROOT_PATH}/Inc)
target_include_directories(Player PUBLIC ${ENGINE_ROOT_PATH}/Ext)
target_include_directories(Player PUBLIC ${ENGINE_ROOT_PATH}/Ext/rapidjson/inc)
target_include_directories(Player PUBLIC ${ENGINE_ROOT_PATH})
target_include_directories(Player PUBLIC ${ENGINE_ROOT_PATH}/Inc)
target_include_directories(Player PUBLIC ${ENGINE_ROOT_PATH}/Ext/tracy/public)
target_include_directories(Player PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)

target_compile_definitions(Player PUBLIC
    _WINDOWS
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>)

if (AILU_ENABLE_TRACY)
    target_compile_definitions(Player PUBLIC TRACY_ENABLE TRACY_IMPORTS)
    if (AILU_TRACY_ON_DEMAND)
        target_compile_definitions(Player PUBLIC TRACY_ON_DEMAND)
    endif()
endif()

if (MSVC)
    target_compile_options(Player PUBLIC /MP /wd4819 /utf-8)
endif()

add_dependencies(Player Engine)
target_link_libraries(Player PRIVATE Engine)

file(GENERATE
    OUTPUT "${PLAYER_RUNTIME_CONFIG_FILE}"
    CONTENT "{\n    \"resourceRoot\": \"${PLAYER_RUNTIME_RESOURCE_ROOT}\",\n    \"startupScene\": \"WaterTest/empty_scene.almap\",\n    \"windowWidth\": 1600,\n    \"windowHeight\": 900,\n    \"developmentMode\": true\n}\n")

add_custom_command(TARGET Player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_RUNTIME_CONFIG_FILE}" "$<TARGET_FILE_DIR:Player>/player.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Editor/EngineConfig.json" "$<TARGET_FILE_DIR:Player>/EngineConfig.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_DXC_DLL_DIR}/dxcompiler.dll" "$<TARGET_FILE_DIR:Player>/dxcompiler.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_DXC_DLL_DIR}/dxil.dll" "$<TARGET_FILE_DIR:Player>/dxil.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_PIX_DLL}" "$<TARGET_FILE_DIR:Player>/WinPixEventRuntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_FBX_DLL}" "$<TARGET_FILE_DIR:Player>/libfbxsdk.dll"
    COMMENT "Preparing Player runtime config"
    VERBATIM)

add_custom_target(package_player
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PLAYER_PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLAYER_PACKAGE_DIR}/Engine"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Player>" "${PLAYER_PACKAGE_DIR}/$<TARGET_FILE_NAME:Player>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "${PLAYER_PACKAGE_DIR}/$<TARGET_FILE_NAME:Engine>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_DXC_DLL_DIR}/dxcompiler.dll" "${PLAYER_PACKAGE_DIR}/dxcompiler.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_DXC_DLL_DIR}/dxil.dll" "${PLAYER_PACKAGE_DIR}/dxil.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_PIX_DLL}" "${PLAYER_PACKAGE_DIR}/WinPixEventRuntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_FBX_DLL}" "${PLAYER_PACKAGE_DIR}/libfbxsdk.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PLAYER_ROOT_PATH}/player.package.json" "${PLAYER_PACKAGE_DIR}/player.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Editor/EngineConfig.json" "${PLAYER_PACKAGE_DIR}/EngineConfig.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Engine/Res" "${PLAYER_PACKAGE_DIR}/Engine/Res"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ENGINE_ROOT_PATH}/Inc/Render/ShaderInterop.h" "${PLAYER_PACKAGE_DIR}/Engine/Res/Shaders/ShaderInterop.h"
    DEPENDS Player
    COMMENT "Staging minimal Player package"
    VERBATIM)
