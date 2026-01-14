# https://blog.csdn.net/weixin_43717839/article/details/128032486
cmake_minimum_required(VERSION 4.1)

project (AiluEditor)
set(EDITOR_ROOT_PATH ${CMAKE_SOURCE_DIR}/Editor)
set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${AiluBuildPath}/out/editor/bin/x64)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${AiluBuildPath}/out/editor/bin/x64/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${AiluBuildPath}/out/editor/bin/x64/release)

set(ENGINE_LIB_ROOT ${AiluBuildPath}/out/engine/lib/x64)
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

add_source_files_recursive(${EDITOR_ROOT_PATH}/Src)
add_source_files_recursive(${EDITOR_ROOT_PATH}/Inc)

add_executable(Editor WIN32 ${PROJECT_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${PROJECT_SOURCES})

# 包含路径
target_include_directories(Editor PUBLIC ${EDITOR_ROOT_PATH}/)
target_include_directories(Editor PUBLIC ${EDITOR_ROOT_PATH}/Inc)
target_include_directories(Editor PUBLIC ${ENGINE_ROOT_PATH}/Ext)
target_include_directories(Editor PUBLIC ${ENGINE_ROOT_PATH})
target_include_directories(Editor PUBLIC ${ENGINE_ROOT_PATH}/Inc)
target_include_directories(Editor PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)
target_include_directories(Editor PUBLIC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo)

target_include_directories(Editor PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk)
target_include_directories(Editor PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk/include)
# 宏定义
target_compile_definitions(Editor PUBLIC _DEBUG;Editor_EXPORTS;_WINDOWS;_REVERSED_Z)
set_target_properties (Editor PROPERTIES OUTPUT_NAME "Editor")
# 编译器参数
# if(MSVC)
    target_compile_options(Editor PUBLIC /MP)
    # MSVC 编译器的警告选项不同，以下是一个示例
    target_compile_options(Editor PUBLIC /D_UNICODE /DUNICODE /wd4819) # /wd4100 禁用未使用的参数警告
# endif()
# 链接库
# 设置不同配置的库路径
#target_link_directories(Editor PRIVATE
#        $<$<CONFIG:Debug>:${ENGINE_LIB_ROOT}/Debug>
#        $<$<CONFIG:Release>:${ENGINE_LIB_ROOT}/Release>
#        )
add_dependencies(Editor Engine)
target_link_libraries(Editor PRIVATE
        $<$<CONFIG:Debug>:${ENGINE_LIB_ROOT}/Debug/Engine_d.lib>
        $<$<CONFIG:Release>:${ENGINE_LIB_ROOT}/Release/Engine.lib> ${ENGINE_ROOT_PATH}/Ext/fbxsdk/lib/vs2019/x64/debug/libfbxsdk.lib) # msvcrtd.lib