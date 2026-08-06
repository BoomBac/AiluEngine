project (Test)
set(TEST_ROOT_PATH ${CMAKE_SOURCE_DIR}/Test)
set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(EDITOR_ROOT_PATH ${CMAKE_SOURCE_DIR}/Editor)
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

function(ailu_configure_test_target target_name)
    target_include_directories(${target_name} PUBLIC ${TEST_ROOT_PATH}/)
    target_include_directories(${target_name} PUBLIC ${TEST_ROOT_PATH}/Inc)
    target_include_directories(${target_name} PUBLIC ${EDITOR_ROOT_PATH}/Inc)
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH}/Ext)
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH})
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH}/Inc)
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH}/Ext/tracy/public)
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH}/Ext/imgui)
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH}/Ext/ImGuizmo)
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk)
    target_include_directories(${target_name} PUBLIC ${ENGINE_ROOT_PATH}/Ext/fbxsdk/include)
    if (AILU_ENABLE_LUA_SCRIPTING)
        target_include_directories(${target_name} PUBLIC ${AILU_SOL2_INCLUDE_DIR})
        target_include_directories(${target_name} PUBLIC ${AILU_LUA_INCLUDE_DIR})
        if (AILU_LUA_SOURCE_DIR)
            target_include_directories(${target_name} PUBLIC ${AILU_LUA_SOURCE_DIR})
        endif()
    endif()

    target_compile_definitions(${target_name} PUBLIC
        Editor_EXPORTS
        _WINDOWS
        UNICODE
        _UNICODE
        AILU_ENABLE_LUA_SCRIPTING=$<BOOL:${AILU_ENABLE_LUA_SCRIPTING}>
        $<$<CONFIG:Debug>:_DEBUG>)

    if (AILU_ENABLE_TRACY)
        target_compile_definitions(${target_name} PUBLIC TRACY_ENABLE TRACY_IMPORTS)
        if (AILU_TRACY_ON_DEMAND)
            target_compile_definitions(${target_name} PUBLIC TRACY_ON_DEMAND)
        endif()
    endif()

    if (MSVC)
        target_compile_options(${target_name} PUBLIC /MP /wd4819)
    endif()

    if (TARGET Engine)
        target_link_libraries(${target_name} PRIVATE Engine)
    else()
        target_link_libraries(${target_name} PRIVATE ${AILU_ENGINE_IMPORT_LIB})
    endif()

    target_link_libraries(${target_name} PRIVATE ${AILU_FBXSDK_LINK_LIB})
endfunction()
add_executable(Test
    "Test.cpp"
    "Test.h"
    "Src/TestAutomationCore.cpp"
    "Src/TestAutomationReadModel.cpp"
    "Src/TestAutomationAdapter.cpp"
    "Src/TestAutomationTransport.cpp"
    "Src/TestAutomationCommand.cpp"
    "Src/TestAIAssistant.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationTypes.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationService.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationReadModel.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationAdapter.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationJson.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationSession.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationPipeServer.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AutomationCommand.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Automation/AIAssistant.cpp")
set_target_properties(Test PROPERTIES OUTPUT_NAME "Test")
ailu_configure_test_target(Test)

if (AILU_ENABLE_LUA_SCRIPTING)
    add_executable(ScriptSmoke "ScriptSmoke.cpp")
    set_target_properties(ScriptSmoke PROPERTIES OUTPUT_NAME "ScriptSmoke")
    target_compile_definitions(ScriptSmoke PRIVATE AILU_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
    ailu_configure_test_target(ScriptSmoke)
endif()