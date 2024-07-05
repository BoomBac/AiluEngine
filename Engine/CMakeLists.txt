# https://blog.csdn.net/weixin_43717839/article/details/128032486
cmake_minimum_required (VERSION 3.5)

project (AiluEngine)

# 设置输出目录的根目录
set(OUTPUT_ROOT "${PROJECT_SOURCE_DIR}/lib/x64")
# set (LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib/x64/${CONFIG})
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
aux_source_directory (${ENGINE_ROOT_PATH}/Ext/imgui IMGUI_ROOT)
set(IMGUI_BACKENDS ${ENGINE_ROOT_PATH}/Ext/imgui/backends/imgui_impl_dx12.cpp;${ENGINE_ROOT_PATH}/Ext/imgui/backends/imgui_impl_win32.cpp)
set(IMGUIZMO_SRC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo/ImGuizmo.cpp)
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

add_library (Engine SHARED ${PROJECT_SOURCES};${IMGUI_ROOT};${IMGUI_BACKENDS};${IMGUIZMO_SRC})
# 包含路径
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Inc)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk/include)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk)
target_include_directories(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo)
# 宏定义
target_compile_definitions(Engine PUBLIC AILU_BUILD_DLL;_DEBUG;ENGINE_EXPORTS;_WINDOWS;_USRDLL;FBXSDK_SHARED;NOMINMAX)
set_target_properties (Engine PROPERTIES
        OUTPUT_NAME_DEBUG Engine_d
        OUTPUT_NAME_RELEASE Engine
        ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>"
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>")
# 编译器参数
if(MSVC)
    target_compile_options(Engine PUBLIC /MP)
    # MSVC 编译器的警告选项不同，以下是一个示例
    target_compile_options(Engine PUBLIC /D_UNICODE /DUNICODE /wd4819) # /wd4100 禁用未使用的参数警告
endif()
target_precompile_headers(Engine PRIVATE Inc/pch.h)
# 链接库
target_link_libraries(Engine PUBLIC ${ENGINE_ROOT_PATH}/Ext/pix/WinPixEventRuntime.lib;d3d12.lib;dxgi.lib;
        ${ENGINE_ROOT_PATH}/Ext/dxc/lib/x64/dxcompiler.lib;d3dcompiler.lib;dxguid.lib;${ENGINE_ROOT_PATH}/Ext/fbxsdk/lib/vs2019/x64/debug/libfbxsdk.lib)

# 添加自定义命令
# 添加自定义命令来运行批处理脚本
add_custom_command(TARGET Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running post-build script..."
        COMMAND cmd /c "${PROJECT_SOURCE_DIR}/post_build_script.bat"
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Running post-build script..."
        VERBATIM
        )