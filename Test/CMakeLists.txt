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
        FBXSDK_SHARED
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
        target_compile_options(${target_name} PUBLIC /MP /utf-8 /wd4819)
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
    "Src/TestAnimation.cpp"
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

    add_executable(ScriptRegression "ScriptRegression.cpp")
    set_target_properties(ScriptRegression PROPERTIES OUTPUT_NAME "ScriptRegression")
    target_compile_definitions(ScriptRegression PRIVATE AILU_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
    ailu_configure_test_target(ScriptRegression)
endif()

# Standalone platform tests have no Editor dependency or runtime initialization.
add_library(AiluDynamicLibraryProbe SHARED Plugins/DynamicLibraryProbe/Probe.cpp)
add_executable(DynamicLibrarySmoke DynamicLibrarySmoke.cpp)
target_link_libraries(DynamicLibrarySmoke PRIVATE Engine)
target_compile_definitions(DynamicLibrarySmoke PRIVATE UNICODE _UNICODE)
if (MSVC)
    target_compile_options(DynamicLibrarySmoke PRIVATE /utf-8)
endif()
add_dependencies(DynamicLibrarySmoke AiluDynamicLibraryProbe)
add_custom_command(TARGET DynamicLibrarySmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:DynamicLibrarySmoke>"
    VERBATIM)

add_library(AiluPluginSmoke SHARED Plugins/SmokePlugin/SmokePlugin.cpp)
target_include_directories(AiluPluginSmoke PRIVATE "${ENGINE_ROOT_PATH}/Inc")
target_compile_definitions(AiluPluginSmoke PRIVATE AILU_BUILD_PLUGIN)
set_target_properties(AiluPluginSmoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${AiluBuildPath}/out/test/bin/x64/$<LOWER_CASE:$<CONFIG>>/Plugins/SmokePlugin")
foreach(plugin_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOLOWER "${plugin_config}" plugin_config_directory)
    set_target_properties(AiluPluginSmoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${plugin_config}
        "${AiluBuildPath}/out/test/bin/x64/${plugin_config_directory}/Plugins/SmokePlugin")
endforeach()
add_executable(PluginSmoke PluginSmoke.cpp Src/TestPluginSystem.cpp)
target_link_libraries(PluginSmoke PRIVATE Engine)
target_compile_definitions(PluginSmoke PRIVATE UNICODE _UNICODE)
add_dependencies(PluginSmoke AiluPluginSmoke)
if (MSVC)
    target_compile_options(AiluPluginSmoke PRIVATE /utf-8)
    target_compile_options(PluginSmoke PRIVATE /utf-8)
endif()
add_custom_command(TARGET PluginSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:PluginSmoke>"
    VERBATIM)

add_library(AiluPluginBadVersion SHARED Plugins/BadVersionPlugin/BadVersionPlugin.cpp)
target_include_directories(AiluPluginBadVersion PRIVATE "${ENGINE_ROOT_PATH}/Inc")
foreach(plugin_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOLOWER "${plugin_config}" plugin_config_directory)
    set_target_properties(AiluPluginBadVersion PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${plugin_config}
        "${AiluBuildPath}/out/test/bin/x64/${plugin_config_directory}/Plugins/BadVersionPlugin")
endforeach()
add_dependencies(PluginSmoke AiluPluginBadVersion)

function(ailu_add_lifecycle_probe target_name mode plugin_id)
    add_library(${target_name} SHARED Plugins/LifecycleProbe/LifecycleProbe.cpp)
    target_include_directories(${target_name} PRIVATE "${ENGINE_ROOT_PATH}/Inc")
    target_compile_definitions(${target_name} PRIVATE TEST_PLUGIN_MODE=${mode} TEST_PLUGIN_ID="${plugin_id}")
    foreach(plugin_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        string(TOLOWER "${plugin_config}" plugin_config_directory)
        set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${plugin_config}
            "${AiluBuildPath}/out/test/bin/x64/${plugin_config_directory}/Plugins/LifecycleProbe")
    endforeach()
endfunction()
ailu_add_lifecycle_probe(AiluPluginLifecycle 0 com.ailu.test.lifecycle)
ailu_add_lifecycle_probe(AiluPluginSecond 0 com.ailu.test.second)
ailu_add_lifecycle_probe(AiluPluginInvalid 1 com.ailu.test.invalid)
ailu_add_lifecycle_probe(AiluPluginNullCreate 2 com.ailu.test.nullcreate)
ailu_add_lifecycle_probe(AiluPluginLoadFail 3 com.ailu.test.loadfail)
ailu_add_lifecycle_probe(AiluPluginMissingDescriptor 4 com.ailu.test.missingdescriptor)
ailu_add_lifecycle_probe(AiluPluginMissingCreate 5 com.ailu.test.missingcreate)
ailu_add_lifecycle_probe(AiluPluginMissingDestroy 6 com.ailu.test.missingdestroy)
add_executable(PluginManagerSmoke PluginManagerSmoke.cpp)
target_link_libraries(PluginManagerSmoke PRIVATE Engine)
target_compile_definitions(PluginManagerSmoke PRIVATE UNICODE _UNICODE)
if (MSVC)
    target_compile_options(PluginManagerSmoke PRIVATE /utf-8)
endif()
add_dependencies(PluginManagerSmoke AiluDynamicLibraryProbe AiluPluginLifecycle AiluPluginSecond
    AiluPluginInvalid AiluPluginNullCreate AiluPluginLoadFail AiluPluginMissingDescriptor
    AiluPluginMissingCreate AiluPluginMissingDestroy)

add_executable(PluginManifestSmoke PluginManifestSmoke.cpp)
target_link_libraries(PluginManifestSmoke PRIVATE Engine)
target_compile_definitions(PluginManifestSmoke PRIVATE UNICODE _UNICODE)
if (MSVC)
    target_compile_options(PluginManifestSmoke PRIVATE /utf-8)
endif()
add_dependencies(PluginManifestSmoke AiluPluginSmoke AiluPluginSecond AiluPluginBadVersion)

add_executable(PluginRegistrationSmoke PluginRegistrationSmoke.cpp)
target_link_libraries(PluginRegistrationSmoke PRIVATE Engine)
target_compile_definitions(PluginRegistrationSmoke PRIVATE UNICODE _UNICODE)
if (MSVC)
    target_compile_options(PluginRegistrationSmoke PRIVATE /utf-8)
endif()
add_dependencies(PluginRegistrationSmoke AiluPluginLifecycle AiluPluginSecond)

# Standalone reproduction of JobGroup completion/destruction races.
add_executable(JobGroupLifetimeSmoke JobGroupLifetimeSmoke.cpp)
ailu_configure_test_target(JobGroupLifetimeSmoke)
target_link_libraries(JobGroupLifetimeSmoke PRIVATE dbghelp)
add_custom_command(TARGET JobGroupLifetimeSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:JobGroupLifetimeSmoke>"
    VERBATIM)

add_executable(VolumetricFogSmoke VolumetricFogSmoke.cpp)
ailu_configure_test_target(VolumetricFogSmoke)
add_custom_command(TARGET VolumetricFogSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:VolumetricFogSmoke>"
    VERBATIM)

target_sources(VolumetricFogSmoke PRIVATE ${ENGINE_ROOT_PATH}/Src/Render/ImageCapture/DdsCaptureEncoder.cpp)
target_link_libraries(VolumetricFogSmoke PRIVATE DirectXTex)
