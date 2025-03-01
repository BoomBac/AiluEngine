# https://blog.csdn.net/weixin_43717839/article/details/128032486
cmake_minimum_required (VERSION 3.5)

project (AiluEngine)
message("AiluEngine generator is set to ${CMAKE_GENERATOR}")
# 设置输出目录的根目录
set(OUTPUT_ROOT "${AiluBuildPath}/out/engine/lib/x64")
# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 自定义递归函数
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

set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)

add_source_files_recursive(${ENGINE_ROOT_PATH}/Src)
add_source_files_recursive(${ENGINE_ROOT_PATH}/Inc)
aux_source_directory (${ENGINE_ROOT_PATH}/Ext/imgui IMGUI_ROOT)
aux_source_directory (${ENGINE_ROOT_PATH}/Ext/implot IMPLOT_ROOT)
set(CORE_MODULE
        Src/Objects/Type.cpp
        Inc/Objects/Type.h
)
set(RENDER_MODULE
        Inc/Render/Gizmo.h
        Src/Render/Gizmo.cpp
        Src/Render/TextRenderer.cpp
        Src/Render/FrameResource.cpp
        Inc/Render/FrameResource.h
        Src/Render/Pass/VoxelGI.cpp
        Inc/Render/Pass/VoxelGI.h)
set(RHI_DX12
        Inc/RHI/DX12/GPUResourceManager.h
        Src/RHI/DX12/GPUResourceManager.cpp
        Inc/RHI/DX12/Page.h
        Src/RHI/DX12/Page.cpp)
set(UI_MODULE
        Inc/UI/UIFramework.h
        Src/UI/UIFramework.cpp
        Src/UI/UIElement.cpp
        Inc/UI/UIElement.h
        Src/UI/Canvas.cpp
        Inc/UI/Canvas.h
        Src/UI/UIRenderer.cpp
        Inc/UI/UIRenderer.h)
set(IMGUI_BACKENDS ${ENGINE_ROOT_PATH}/Ext/imgui/backends/imgui_impl_dx12.cpp;${ENGINE_ROOT_PATH}/Ext/imgui/backends/imgui_impl_win32.cpp)
set(IMGUIZMO_SRC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo/ImGuizmo.cpp)
set(DDS_LOADER ${ENGINE_ROOT_PATH}/Ext/dds_loader/DDSTextureLoader12.cpp)
set(ANIM_MODULE
        Src/Animation/Curve.cpp
        Inc/Animation/Track.hpp
        Src/Animation/Track.cpp
        Src/Animation/TransformTrack.cpp
        Src/Animation/CrossFade.cpp
        Src/Animation/Solver.cpp "Src/Animation/BlendSpace.cpp")
# set(IMGUI_PLOT ${ENGINE_ROOT_PATH}/Ext/implot/implot.cpp ${ENGINE_ROOT_PATH}/Ext/implot/implot_items.cpp)
# 禁用特定文件的预编译头
# 使用 file(GLOB_RECURSE) 获取所有源文件
file(GLOB_RECURSE NO_PCH_SOURCES "${ENGINE_ROOT_PATH}/Ext/imgui/*.cpp")
# 为每个文件设置属性以禁用预编译头
foreach(SOURCE_FILE IN LISTS NO_PCH_SOURCES)
    set_source_files_properties(${SOURCE_FILE} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endforeach()

set_source_files_properties(IMGUIZMO_SRC PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
# set_source_files_properties(IMGUI_BACKENDS PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
# aux_source_directory (Src engine_src_dir)
# 为目标添加 include 目录
add_custom_target(pre_build_cmd ALL
        COMMAND ${CMAKE_COMMAND} -E echo "Running pre-build script..."
        COMMAND cmd /c "${PROJECT_SOURCE_DIR}/pre_build_script.bat")

add_library (Engine SHARED ${PROJECT_SOURCES};${IMGUI_ROOT};${IMPLOT_ROOT};${IMGUI_BACKENDS};${IMGUIZMO_SRC};${RENDER_MODULE};${RHI_DX12};${UI_MODULE};${DDS_LOADER} ${IMGUI_PLOT} ${ANIM_MODULE}
"Inc/Scene/Scene.h" "Src/Scene/Scene.cpp" "Inc/Scene/RenderSystem.h" "Src/Scene/RenderSystem.cpp" "Inc/RHI/DX12/D3DUtils.h"
"Src/Scene/Component.cpp" "Inc/Framework/Parser/DDSParser.h" "Src/Framework/Parser/DDSParser.cpp" "Inc/Physics/PhysicsSystem.h" "Src/Physics/PhysicsSystem.cpp" "Src/Render/Gizmo.cpp" "Inc/Physics/Collision.h" "Src/Physics/Collision.cpp" "Inc/Animation/AnimationSystem.h" "Src/Animation/AnimationSystem.cpp"
        Inc/Framework/Common/JobSystem.h
        Src/Framework/Common/JobSystem.cpp
        Ext/imnodes/imnodes.cpp
        Src/Render/Font.cpp)
add_dependencies(Engine pre_build_cmd)
# 包含路径
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Inc)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk/include)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/implot)
# 宏定义
target_compile_definitions(Engine PUBLIC AILU_BUILD_DLL;_DEBUG;ENGINE_EXPORTS;_WINDOWS;_USRDLL;FBXSDK_SHARED;NOMINMAX;_REVERSED_Z)
set_target_properties (Engine PROPERTIES
        OUTPUT_NAME_DEBUG Engine_d
        OUTPUT_NAME_RELEASE Engine
        ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>"
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>")
# 编译器参数
# if(MSVC)
    target_compile_options(Engine PUBLIC /MP)
    # MSVC 编译器的警告选项不同，以下是一个示例
    target_compile_options(Engine PUBLIC /D_UNICODE /DUNICODE /wd4819 /wd4251) # /wd4100 禁用未使用的参数警告
# endif()
target_precompile_headers(Engine PRIVATE Inc/pch.h)
# 链接库
target_link_libraries(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/pix/WinPixEventRuntime.lib;d3d12.lib;dxgi.lib;d2d1.lib;dwrite.lib;d3d11.lib;
        ${ENGINE_ROOT_PATH}/Ext/dxc/lib/x64/dxcompiler.lib;d3dcompiler.lib;dxguid.lib;${ENGINE_ROOT_PATH}/Ext/fbxsdk/lib/vs2019/x64/debug/libfbxsdk.lib) # ;msvcrtd.lib


# 添加自定义命令
# 添加自定义命令来运行批处理脚本
add_custom_command(TARGET Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running post-build script..."
        COMMAND cmd /c "${PROJECT_SOURCE_DIR}/post_build_script.bat"
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