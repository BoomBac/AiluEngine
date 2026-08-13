# https://blog.csdn.net/weixin_43717839/article/details/128032486
cmake_minimum_required(VERSION 4.1)
project (AiluEngine)
option(AILU_ENABLE_LUA_SCRIPTING "Enable Lua + sol2 scripting support" OFF)
set(AILU_SOL2_INCLUDE_DIR "" CACHE PATH "Path to sol2 include root containing sol/sol.hpp")
set(AILU_LUA_INCLUDE_DIR "" CACHE PATH "Path to Lua headers containing lua.hpp")
set(AILU_LUA_LIBRARY "" CACHE FILEPATH "Path to the Lua library file")
set(AILU_LUA_SOURCE_DIR "" CACHE PATH "Path to vendored Lua source tree")
# 设置输出目录的根目录
set(OUTPUT_ROOT "${AiluBuildPath}/out/engine/lib/x64")
# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 自定义递归函数
function(add_source_files_recursive dir)
        file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS RELATIVE ${dir} ${dir}/*.cpp ${dir}/*.hpp ${dir}/*.h ${dir}/*.inl)
    foreach(source ${SOURCES})
        if(IS_DIRECTORY ${dir}/${source})
            add_source_files_recursive(${dir}/${source})
        else()
            list(APPEND PROJECT_SOURCES ${dir}/${source})
        endif()
    endforeach()
    set(PROJECT_SOURCES ${PROJECT_SOURCES} PARENT_SCOPE)
endfunction()

set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(AILU_LUA_SOURCES)
set(AILU_LUA_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/lua_include)

# Physics2D uses Box2D as its sole runtime backend. Keep it an implementation detail of Engine.
add_subdirectory(${ENGINE_ROOT_PATH}/Ext/box2d)

set(AILU_LUA_SCRIPTING_ENABLED 0)
if (AILU_ENABLE_LUA_SCRIPTING)
        if (NOT AILU_SOL2_INCLUDE_DIR AND EXISTS ${ENGINE_ROOT_PATH}/Ext/sol2/include/sol/sol.hpp)
                set(AILU_SOL2_INCLUDE_DIR ${ENGINE_ROOT_PATH}/Ext/sol2/include CACHE PATH "Path to sol2 include root containing sol/sol.hpp" FORCE)
        endif()
        if (NOT AILU_SOL2_INCLUDE_DIR)
                find_path(AILU_SOL2_INCLUDE_DIR sol/sol.hpp
                        PATHS
                        ${ENGINE_ROOT_PATH}/Ext/sol2/include
                        ${ENGINE_ROOT_PATH}/Ext/sol2/single/include)
        endif()
        if (NOT AILU_LUA_SOURCE_DIR AND EXISTS ${ENGINE_ROOT_PATH}/Ext/lua/onelua.c)
                set(AILU_LUA_SOURCE_DIR ${ENGINE_ROOT_PATH}/Ext/lua CACHE PATH "Path to vendored Lua source tree" FORCE)
        endif()
        if (AILU_LUA_SOURCE_DIR AND EXISTS ${AILU_LUA_SOURCE_DIR}/onelua.c)
                file(MAKE_DIRECTORY ${AILU_LUA_GENERATED_INCLUDE_DIR})
                set(AILU_LUA_HPP_CONTENT "#pragma once\n\nextern \"C\" {\n#include \"lua.h\"\n#include \"lauxlib.h\"\n#include \"lualib.h\"\n}\n")
                set(AILU_LUA_HPP_PATH ${AILU_LUA_GENERATED_INCLUDE_DIR}/lua.hpp)
                set(AILU_WRITE_LUA_HPP ON)
                if (EXISTS ${AILU_LUA_HPP_PATH})
                        file(READ ${AILU_LUA_HPP_PATH} AILU_EXISTING_LUA_HPP_CONTENT)
                        if (AILU_EXISTING_LUA_HPP_CONTENT STREQUAL AILU_LUA_HPP_CONTENT)
                                set(AILU_WRITE_LUA_HPP OFF)
                        endif()
                endif()
                if (AILU_WRITE_LUA_HPP)
                        file(WRITE ${AILU_LUA_HPP_PATH} ${AILU_LUA_HPP_CONTENT})
                endif()
                set(AILU_LUA_INCLUDE_DIR ${AILU_LUA_GENERATED_INCLUDE_DIR} CACHE PATH "Path to Lua headers containing lua.hpp" FORCE)
        endif()
        if (NOT AILU_LUA_INCLUDE_DIR)
                find_path(AILU_LUA_INCLUDE_DIR lua.hpp
                        PATHS
                        ${AILU_LUA_GENERATED_INCLUDE_DIR}
                        ${AILU_LUA_SOURCE_DIR}
                        ${ENGINE_ROOT_PATH}/Ext/lua/include
                        ${ENGINE_ROOT_PATH}/Ext/lua/src
                        ${ENGINE_ROOT_PATH}/Ext/lua)
        endif()
        if (NOT AILU_LUA_LIBRARY AND NOT AILU_LUA_SOURCE_DIR)
                find_library(AILU_LUA_LIBRARY
                        NAMES lua54 lua5.4 lua
                        PATHS
                        ${ENGINE_ROOT_PATH}/Ext/lua/lib
                        ${ENGINE_ROOT_PATH}/Ext/lua/lib/x64)
        endif()
        if (AILU_LUA_SOURCE_DIR AND EXISTS ${AILU_LUA_SOURCE_DIR}/onelua.c)
                set(AILU_LUA_SOURCES ${AILU_LUA_SOURCE_DIR}/onelua.c)
        endif()
        if (AILU_SOL2_INCLUDE_DIR AND AILU_LUA_INCLUDE_DIR AND (AILU_LUA_LIBRARY OR AILU_LUA_SOURCES))
                set(AILU_LUA_SCRIPTING_ENABLED 1)
        else()
                message(WARNING "AILU_ENABLE_LUA_SCRIPTING is ON, but sol2 or Lua paths are missing. ScriptSystem will compile in stub mode.")
        endif()
endif()

add_source_files_recursive(${ENGINE_ROOT_PATH}/Src)
add_source_files_recursive(${ENGINE_ROOT_PATH}/Inc)
list(APPEND PROJECT_SOURCES ${AILU_ENGINE_GENERATED_SOURCES})
# 文件夹结构映射
# source_group(TREE "${ENGINE_ROOT_PATH}/Src" PREFIX "Src" FILES ${PROJECT_SOURCES})
# source_group(TREE "${ENGINE_ROOT_PATH}/Inc" PREFIX "Inc" FILES ${PROJECT_SOURCES})

aux_source_directory (${ENGINE_ROOT_PATH}/Ext/imgui IMGUI_ROOT)
aux_source_directory (${ENGINE_ROOT_PATH}/Ext/implot IMPLOT_ROOT)
set(CORE_MODULE
        Src/Objects/Type.cpp
        Inc/Objects/Type.h
        Inc/Framework/Common/Container.hpp
        Inc/Framework/Common/EngineConfig.h
        Inc/Framework/Common/Allocator.hpp
        Inc/Framework/Common/Allocator.cpp
        Inc/Framework/Common/StackTrace.cpp
)
set(RENDER_MODULE
        Inc/Render/Gizmo.h
        Src/Render/Gizmo.cpp
        Src/Render/FrameResource.cpp
        Inc/Render/FrameResource.h
        Src/Render/Features/VoxelGI.cpp
        Inc/Render/Features/VoxelGI.h
        Src/Render/Features/GpuTerrain.cpp
        Src/Render/GfxCommand.cpp
)
set(RHI_DX12
        Inc/RHI/DX12/D3DResourceBase.h
        Inc/RHI/DX12/GPUResourceManager.h
        Src/RHI/DX12/GPUResourceManager.cpp
        Inc/RHI/DX12/Page.h
        Src/RHI/DX12/Page.cpp)
set(UI_MODULE
        Inc/UI/UIFramework.h
        Src/UI/UIFramework.cpp
        Src/UI/UIElement.cpp
        Inc/UI/UIElement.h
        Src/UI/UISlot.cpp
        Inc/UI/UISlot.h
        Src/UI/UIRenderer.cpp
        Inc/UI/UIRenderer.h)
set(IMGUI_BACKENDS ${ENGINE_ROOT_PATH}/Ext/imgui/backends/imgui_impl_dx12.cpp;${ENGINE_ROOT_PATH}/Ext/imgui/backends/imgui_impl_win32.cpp)
set(IMGUIZMO_SRC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo/ImGuizmo.cpp)
set(DDS_LOADER ${ENGINE_ROOT_PATH}/Ext/dds_loader/DDSTextureLoader12.cpp)
set(HDR_LOADER ${ENGINE_ROOT_PATH}/Ext/miniz.cpp)
set(MINIAUDIO_SRC ${ENGINE_ROOT_PATH}/Ext/miniaudio/miniaudio.c)
set(ANIM_MODULE
        Src/Animation/Curve.cpp
        Inc/Animation/Track.hpp
        Src/Animation/Track.cpp
        Src/Animation/TransformTrack.cpp
        Src/Animation/CrossFade.cpp
        Src/Animation/Solver.cpp "Src/Animation/BlendSpace.cpp")
set(IMNODE Ext/imnodes/imnodes.cpp Ext/imnodes/imnodes.h Ext/imnodes/imnodes_internal.h)
# set(IMGUI_PLOT ${ENGINE_ROOT_PATH}/Ext/implot/implot.cpp ${ENGINE_ROOT_PATH}/Ext/implot/implot_items.cpp)
# 禁用特定文件的预编译头
# 使用 file(GLOB_RECURSE) 获取所有源文件
file(GLOB_RECURSE NO_PCH_SOURCES "${ENGINE_ROOT_PATH}/Ext/imgui/*.cpp")
# 为每个文件设置属性以禁用预编译头
foreach(SOURCE_FILE IN LISTS NO_PCH_SOURCES)
    set_source_files_properties(${SOURCE_FILE} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endforeach()
if (AILU_LUA_SOURCES)
        set_source_files_properties(${AILU_LUA_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

set_source_files_properties(IMGUIZMO_SRC PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${MINIAUDIO_SRC} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
# set_source_files_properties(IMGUI_BACKENDS PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

set(ENGINE_THIRD_PARTY_SOURCES
        ${IMGUI_ROOT}
        ${IMPLOT_ROOT}
        ${IMGUI_BACKENDS}
        ${IMGUIZMO_SRC}
        ${DDS_LOADER}
        ${HDR_LOADER}
        ${MINIAUDIO_SRC}
        ${IMNODE}
        ${AILU_LUA_SOURCES}
        ${ENGINE_ROOT_PATH}/Ext/tracy/public/TracyClient.cpp)
set_source_files_properties(${ENGINE_THIRD_PARTY_SOURCES}
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(${AILU_ENGINE_GENERATED_SOURCES}
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# aux_source_directory (Src engine_src_dir)
# 为目标添加 include 目录

# add_library (Engine SHARED ${PROJECT_SOURCES};${IMGUI_ROOT};${IMPLOT_ROOT};${IMGUI_BACKENDS};${IMGUIZMO_SRC};${RENDER_MODULE};${RHI_DX12};${UI_MODULE};${DDS_LOADER} ${IMGUI_PLOT} ${ANIM_MODULE}
# ${HDR_LOADER}
# "Inc/Scene/Scene.h" "Src/Scene/Scene.cpp" "Inc/Scene/RenderSystem.h" "Src/Scene/RenderSystem.cpp"
# "Src/Scene/Component.cpp" "Inc/Framework/Parser/DDSParser.h" "Src/Framework/Parser/DDSParser.cpp" "Inc/Physics/PhysicsSystem.h" "Src/Physics/PhysicsSystem.cpp" "Src/Render/Gizmo.cpp" "Inc/Physics/Collision.h" "Src/Physics/Collision.cpp" "Inc/Animation/AnimationSystem.h" "Src/Animation/AnimationSystem.cpp"
#         Inc/Framework/Common/JobSystem.h
#         Src/Framework/Common/JobSystem.cpp
#         Ext/imnodes/imnodes.cpp
#         Src/Render/Font.cpp)
set(SOURCE_LIST ${PROJECT_SOURCES};${IMGUI_ROOT};${IMPLOT_ROOT};${IMGUI_BACKENDS};${IMGUIZMO_SRC};${RENDER_MODULE};${RHI_DX12};${UI_MODULE};${DDS_LOADER} ${IMGUI_PLOT} ${ANIM_MODULE}
${HDR_LOADER} ${MINIAUDIO_SRC} ${IMNODE} ${AILU_LUA_SOURCES} "Ext/tracy/public/TracyClient.cpp")

add_library (Engine SHARED ${SOURCE_LIST})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})
set_target_properties(Engine PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
        UNITY_BUILD ${AILU_ENABLE_UNITY_BUILD}
        UNITY_BUILD_MODE BATCH
        UNITY_BUILD_BATCH_SIZE ${AILU_UNITY_BUILD_BATCH_SIZE})

add_dependencies(Engine ailu_reflection_codegen)
# 包含路径
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Inc)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/tracy/public)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk/include)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/implot)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/rapidjson)
if (AILU_LUA_SCRIPTING_ENABLED)
        target_include_directories(Engine PUBLIC ${AILU_SOL2_INCLUDE_DIR})
        target_include_directories(Engine PUBLIC ${AILU_LUA_INCLUDE_DIR})
        if (AILU_LUA_SOURCE_DIR)
                target_include_directories(Engine PUBLIC ${AILU_LUA_SOURCE_DIR})
        endif()
endif()
# 宏定义
target_compile_definitions(Engine PRIVATE
    AILU_BUILD_DLL
    ENGINE_EXPORTS
    _WINDOWS
    _USRDLL
    UNICODE
    _UNICODE
    FBXSDK_SHARED
    _REVERSED_Z
    DEAR_IMGUI
    $<$<CONFIG:Debug>:_DEBUG>
    AILU_ENABLE_ASSERTS=$<BOOL:${AILU_ENABLE_ASSERTS}>
    AILU_ENABLE_ENGINE_PROFILER=$<BOOL:${AILU_ENABLE_ENGINE_PROFILER}>
    AILU_ENABLE_D3D_DEBUG_LAYER=$<BOOL:${AILU_ENABLE_D3D_DEBUG_LAYER}>
    AILU_ENABLE_PIX_MARKERS=$<BOOL:${AILU_ENABLE_PIX_MARKERS}>)

target_compile_definitions(Engine PUBLIC AILU_ENABLE_LUA_SCRIPTING=${AILU_LUA_SCRIPTING_ENABLED})

if (AILU_ENABLE_TRACY)
        target_compile_definitions(Engine PRIVATE TRACY_ENABLE)
        # Engine is a DLL, so it hosts the Tracy client and exports symbols for other modules.
        target_compile_definitions(Engine PRIVATE TRACY_EXPORTS)
        if (AILU_TRACY_ON_DEMAND)
                target_compile_definitions(Engine PRIVATE TRACY_ON_DEMAND)
        endif()
endif()
set_target_properties (Engine PROPERTIES
        OUTPUT_NAME_DEBUG Engine_d
        OUTPUT_NAME_RELEASE Engine
        ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>"
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>")
# 编译器参数
if(MSVC)
    target_compile_options(Engine PRIVATE
        /MP
        /bigobj
        /wd4819
        /wd4251
        /utf-8
        $<$<CONFIG:RelWithDebInfo>:/O1>
        $<$<CONFIG:RelWithDebInfo>:/Ob1>
)

    target_compile_options(Engine PRIVATE
        $<$<CONFIG:Debug>:/RTC1>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(Engine PRIVATE -Winvalid-pch)
endif()

target_precompile_headers(Engine PRIVATE Inc/pch.h)
# 链接库
target_link_libraries(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/pix/WinPixEventRuntime.lib;d3d12.lib;dxgi.lib;
d2d1.lib;dwrite.lib;d3d11.lib;
imm32.lib;
        ${ENGINE_ROOT_PATH}/Ext/dxc/lib/x64/dxcompiler.lib;d3dcompiler.lib;dxguid.lib;
        ${AILU_FBXSDK_LINK_LIB}) # ;msvcrtd.lib
target_link_libraries(Engine PRIVATE box2d::box2d)
if (AILU_LUA_SCRIPTING_ENABLED)
        if (AILU_LUA_LIBRARY)
                target_link_libraries(Engine PUBLIC ${AILU_LUA_LIBRARY})
        endif()
endif()

if (WIN32 AND AILU_ENABLE_TRACY)
        target_link_libraries(Engine PRIVATE ws2_32 dbghelp)
endif()


# 添加自定义命令
# 添加自定义命令来运行批处理脚本
add_custom_command(TARGET Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running post-build script..."
        COMMAND cmd /c "${PROJECT_SOURCE_DIR}/post_build_script.bat" "$<CONFIG>" "$<TARGET_FILE:Engine>" "${AiluBuildPath}"
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Running post-build script..."
        VERBATIM
        )
#add_custom_command(TARGET Engine PRE_BUILD
#        COMMAND ${CMAKE_COMMAND} -E echo "Running pre-build script..."
#        COMMAND cmd /c "${PROJECT_SOURCE_DIR}/pre_build_script.bat"
#        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
#        COMMENT "Running pre-build script..."
#        VERBATIM
#)
