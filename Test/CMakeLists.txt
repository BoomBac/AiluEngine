project (Test)
set(TEST_ROOT_PATH ${CMAKE_SOURCE_DIR}/Test)
set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${AiluBuildPath}/out/test/bin/x64)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${AiluBuildPath}/out/test/bin/x64/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${AiluBuildPath}/out/test/bin/x64/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${AiluBuildPath}/out/test/bin/x64/relwithdebinfo)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${AiluBuildPath}/out/test/bin/x64/minsizerel)
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
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/tracy/public)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk)
target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk/include)
# 宏定义
target_compile_definitions(Test PUBLIC
    Editor_EXPORTS
    _WINDOWS
    $<$<CONFIG:Debug>:_DEBUG>)

if (AILU_ENABLE_TRACY)
    target_compile_definitions(Test PUBLIC TRACY_ENABLE TRACY_IMPORTS)
    if (AILU_TRACY_ON_DEMAND)
        target_compile_definitions(Test PUBLIC TRACY_ON_DEMAND)
    endif()
endif()
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
    ${AILU_ENGINE_IMPORT_LIB}
    ${AILU_FBXSDK_LINK_LIB})