project (Test)
set(TEST_ROOT_PATH ${CMAKE_SOURCE_DIR}/Test)
set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/x64)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/x64/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/x64/release)

set(ENGINE_LIB_ROOT ${ENGINE_ROOT_PATH}/lib/x64)
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

add_source_files_recursive(${TEST_ROOT_PATH}/Src)
#aux_source_directory (${CMAKE_SOURCE_DIR}/Ext/imgui IMGUI_ROOT)
#set(IMGUI_BACKENDS ${CMAKE_SOURCE_DIR}/Ext/imgui/backends/imgui_impl_dx12.cpp;${CMAKE_SOURCE_DIR}/Ext/imgui/backends/imgui_impl_win32.cpp)
# aux_source_directory (Src Editor_src_dir)
# 为目标添加 include 目录

add_executable(Test "Test.cpp" "Test.h")

# 包含路径
target_include_directories(Test PUBLIC ${TEST_ROOT_PATH}/)
target_include_directories(Test PUBLIC ${TEST_ROOT_PATH}/Inc)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH})
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Inc)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk/include)
# 宏定义
target_compile_definitions(Test PUBLIC _DEBUG;Editor_EXPORTS;_WINDOWS)
set_target_properties (Test PROPERTIES OUTPUT_NAME "Test")
# 编译器参数
# if(MSVC)
    target_compile_options(Test PUBLIC /MP)
    # MSVC 编译器的警告选项不同，以下是一个示例
    target_compile_options(Test PUBLIC /D_UNICODE /DUNICODE /wd4819) # /wd4100 禁用未使用的参数警告
# endif()
# 链接库
# 设置不同配置的库路径
#target_link_directories(Test PRIVATE
#        $<$<CONFIG:Debug>:${ENGINE_LIB_ROOT}/Debug>
#        $<$<CONFIG:Release>:${ENGINE_LIB_ROOT}/Release>
#        )
target_link_libraries(Test PRIVATE
        $<$<CONFIG:Debug>:${ENGINE_LIB_ROOT}/Debug/Engine_d.lib>
        $<$<CONFIG:Release>:${ENGINE_LIB_ROOT}/Release/Engine.lib> ${ENGINE_ROOT_PATH}/Ext/fbxsdk/lib/vs2019/x64/debug/libfbxsdk.lib)