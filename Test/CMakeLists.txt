project (Test)
set(TEST_ROOT_PATH ${CMAKE_SOURCE_DIR}/Test)
set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(EDITOR_ROOT_PATH ${CMAKE_SOURCE_DIR}/Editor)

# Standalone M2 executable: no Engine DLL, CUDA SDK headers or OIDN import library.
add_executable(OidnGpuInteropSpike OidnGpuInteropSpike.cpp
    "${ENGINE_ROOT_PATH}/Src/RHI/DX12/OidnGpuInteropD3D12.cpp")
target_include_directories(OidnGpuInteropSpike PRIVATE "${ENGINE_ROOT_PATH}/Src")
target_compile_definitions(OidnGpuInteropSpike PRIVATE AILU_WITH_OIDN_GPU=$<BOOL:${AILU_WITH_OIDN_GPU}>)
target_link_libraries(OidnGpuInteropSpike PRIVATE d3d12 dxgi d3dcompiler)
if(MSVC)
    target_compile_options(OidnGpuInteropSpike PRIVATE /utf-8)
endif()
set_target_properties(OidnGpuInteropSpike PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${AiluBuildPath}/out/test/bin/x64/debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${AiluBuildPath}/out/test/bin/x64/release")
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

add_executable(CameraTemporalHistorySmoke "CameraTemporalHistorySmoke.cpp")
ailu_configure_test_target(CameraTemporalHistorySmoke)
add_custom_command(TARGET CameraTemporalHistorySmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:CameraTemporalHistorySmoke>"
    VERBATIM)

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

add_executable(LogSmoke LogSmoke.cpp)
target_link_libraries(LogSmoke PRIVATE Engine)
target_compile_definitions(LogSmoke PRIVATE UNICODE _UNICODE)
if (MSVC)
    target_compile_options(LogSmoke PRIVATE /utf-8)
endif()
add_custom_command(TARGET LogSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:LogSmoke>"
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
add_executable(InspectorMultiEditSmoke InspectorMultiEditSmoke.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/TransformPropertyCommand.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/ComponentEditorRegistry.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/MultiComponentEditor.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/ComponentInspectorFields.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/ReflectedInspectorFields.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/ScriptInspectorFields.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/Collider2DInspectorFields.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/MeshInspectorFields.cpp
    ${CMAKE_SOURCE_DIR}/Editor/Src/Inspector/ComponentEditors/TransformComponentEditor.cpp)
ailu_configure_test_target(InspectorMultiEditSmoke)
add_custom_command(TARGET InspectorMultiEditSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:InspectorMultiEditSmoke>")
ailu_configure_test_target(VolumetricFogSmoke)
add_custom_command(TARGET VolumetricFogSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:VolumetricFogSmoke>"
    VERBATIM)

target_sources(VolumetricFogSmoke PRIVATE ${ENGINE_ROOT_PATH}/Src/Render/ImageCapture/DdsCaptureEncoder.cpp)
target_link_libraries(VolumetricFogSmoke PRIVATE DirectXTex)

add_executable(MotionVectorGpuAcceptance MotionVectorGpuAcceptance.cpp)
add_executable(DescriptorPageGrowthSmoke DescriptorPageGrowthSmoke.cpp
    ${ENGINE_ROOT_PATH}/Src/RHI/DX12/DescriptorManager.cpp)
ailu_configure_test_target(DescriptorPageGrowthSmoke)
target_link_libraries(DescriptorPageGrowthSmoke PRIVATE d3d12)
add_custom_command(TARGET DescriptorPageGrowthSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:DescriptorPageGrowthSmoke>"
    VERBATIM)
ailu_configure_test_target(MotionVectorGpuAcceptance)
target_link_libraries(MotionVectorGpuAcceptance PRIVATE d3d12)
add_custom_command(TARGET MotionVectorGpuAcceptance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:MotionVectorGpuAcceptance>"
    VERBATIM)

add_executable(TaaGpuAcceptance TaaGpuAcceptance.cpp
    ${ENGINE_ROOT_PATH}/Src/Render/Features/TemporalAA.cpp)
ailu_configure_test_target(TaaGpuAcceptance)
target_link_libraries(TaaGpuAcceptance PRIVATE d3d12)
add_custom_command(TARGET TaaGpuAcceptance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:TaaGpuAcceptance>"
    VERBATIM)

# UI interaction smoke uses an isolated initialized graphics runtime.
add_executable(UISliderHoverSmoke UISliderHoverSmoke.cpp)
ailu_configure_test_target(UISliderHoverSmoke)
add_custom_command(TARGET UISliderHoverSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:UISliderHoverSmoke>"
    VERBATIM)

# Editor progress state tests require no window, renderer or initialized runtime.
add_executable(EditorProgressSmoke EditorProgressSmoke.cpp "${CMAKE_SOURCE_DIR}/Editor/Src/Common/EditorProgressService.cpp")
target_include_directories(EditorProgressSmoke PRIVATE "${EDITOR_ROOT_PATH}/Inc" "${ENGINE_ROOT_PATH}/Inc")
target_compile_features(EditorProgressSmoke PRIVATE cxx_std_20)
if (MSVC)
    target_compile_options(EditorProgressSmoke PRIVATE /utf-8)
endif()

add_executable(AssetImportQueueSmoke AssetImportQueueSmoke.cpp
    "${CMAKE_SOURCE_DIR}/Editor/Src/Widgets/AssetImportController.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Common/EditorProgressService.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Src/Common/EditorPopup.cpp")
ailu_configure_test_target(AssetImportQueueSmoke)
add_custom_command(TARGET AssetImportQueueSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:AssetImportQueueSmoke>"
    VERBATIM)

add_executable(BlenderMeshCacheSmoke BlenderMeshCacheSmoke.cpp)
ailu_configure_test_target(BlenderMeshCacheSmoke)
add_custom_command(TARGET BlenderMeshCacheSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:BlenderMeshCacheSmoke>"
    VERBATIM)

add_executable(BlenderPersistenceSmoke BlenderPersistenceSmoke.cpp)
ailu_configure_test_target(BlenderPersistenceSmoke)
add_custom_command(TARGET BlenderPersistenceSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:BlenderPersistenceSmoke>"
    VERBATIM)

add_executable(BlenderCameraSmoke BlenderCameraSmoke.cpp
    "${ENGINE_ROOT_PATH}/Src/Framework/Math/Transform.cpp"
    "${EDITOR_ROOT_PATH}/Src/Common/CameraControllers.cpp")
ailu_configure_test_target(BlenderCameraSmoke)
add_custom_command(TARGET BlenderCameraSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:BlenderCameraSmoke>"
    VERBATIM)

add_executable(StreamlineDx12BridgeSmoke StreamlineDx12BridgeSmoke.cpp
    "${ENGINE_ROOT_PATH}/Src/RHI/DX12/D3DGPUTimer.cpp"
    "${ENGINE_ROOT_PATH}/Src/RHI/DX12/StreamlineDx12Bridge.cpp"
    "${ENGINE_ROOT_PATH}/Src/RHI/DX12/StreamlineRuntime.cpp")
ailu_configure_test_target(StreamlineDx12BridgeSmoke)
target_compile_definitions(StreamlineDx12BridgeSmoke PRIVATE AILU_WITH_STREAMLINE=0)
target_link_libraries(StreamlineDx12BridgeSmoke PRIVATE d3d12 dxgi)
add_custom_command(TARGET StreamlineDx12BridgeSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:StreamlineDx12BridgeSmoke>"
    VERBATIM)

add_executable(D3DDepthStencilSmoke D3DDepthStencilSmoke.cpp "${ENGINE_ROOT_PATH}/Src/Render/PipelineState.cpp")

add_executable(PathTracingDenoiserSmoke PathTracingDenoiserSmoke.cpp)
add_executable(OidnGpuRenderGraphSmoke OidnGpuRenderGraphSmoke.cpp)
ailu_configure_test_target(OidnGpuRenderGraphSmoke)
target_link_libraries(OidnGpuRenderGraphSmoke PRIVATE d3d12 dxgi)
add_custom_command(TARGET OidnGpuRenderGraphSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:OidnGpuRenderGraphSmoke>"
    VERBATIM)
ailu_configure_test_target(PathTracingDenoiserSmoke)
add_custom_command(TARGET PathTracingDenoiserSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:PathTracingDenoiserSmoke>"
    VERBATIM)
target_include_directories(D3DDepthStencilSmoke PRIVATE "${ENGINE_ROOT_PATH}/Src")
ailu_configure_test_target(D3DDepthStencilSmoke)
add_custom_command(TARGET D3DDepthStencilSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:D3DDepthStencilSmoke>"
    VERBATIM)

add_executable(ObjectBoxBumpSmoke ObjectBoxBumpSmoke.cpp)
target_include_directories(ObjectBoxBumpSmoke PRIVATE "${ENGINE_ROOT_PATH}")
target_link_libraries(ObjectBoxBumpSmoke PRIVATE d3d12 dxgi)
if(MSVC)
    target_compile_options(ObjectBoxBumpSmoke PRIVATE /utf-8)
endif()

add_executable(SamplerSmoke SamplerSmoke.cpp)
ailu_configure_test_target(SamplerSmoke)
add_custom_command(TARGET SamplerSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:SamplerSmoke>"
    VERBATIM)

add_custom_command(TARGET SamplerSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AILU_EXT_PACKAGES_DIR}/fbxsdk/lib/vs2019/x64/$<IF:$<CONFIG:Debug>,debug,release>/libfbxsdk.dll" "$<TARGET_FILE_DIR:SamplerSmoke>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ENGINE_ROOT_PATH}/Ext/pix/WinPixEventRuntime.dll" "$<TARGET_FILE_DIR:SamplerSmoke>"
    VERBATIM)

add_executable(ToneMappingSmoke ToneMappingSmoke.cpp)
ailu_configure_test_target(ToneMappingSmoke)
target_compile_definitions(ToneMappingSmoke PRIVATE AILU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
set(tone_mapping_fbxsdk_directory "${AILU_EXT_PACKAGES_DIR}/fbxsdk/lib/vs2019/x64")
add_custom_command(TARGET ToneMappingSmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:ToneMappingSmoke>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${tone_mapping_fbxsdk_directory}/$<IF:$<CONFIG:Debug>,debug,release>/libfbxsdk.dll"
        "$<TARGET_FILE_DIR:ToneMappingSmoke>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ENGINE_ROOT_PATH}/Ext/pix/WinPixEventRuntime.dll" "$<TARGET_FILE_DIR:ToneMappingSmoke>"
    VERBATIM)

# Keep standalone regression/GPU smoke targets available, but out of the normal ALL build.
set(AILU_TEST_TARGETS
    ObjectBoxBumpSmoke
    ToneMappingSmoke
    Test
    OidnGpuInteropSpike
    CameraTemporalHistorySmoke
    ScriptSmoke
    ScriptRegression
    DynamicLibrarySmoke
    LogSmoke
    AiluDynamicLibraryProbe
    PluginSmoke
    AiluPluginSmoke
    AiluPluginBadVersion
    AiluPluginLifecycle
    AiluPluginSecond
    AiluPluginInvalid
    AiluPluginNullCreate
    AiluPluginLoadFail
    AiluPluginMissingDescriptor
    AiluPluginMissingCreate
    AiluPluginMissingDestroy
    PluginManagerSmoke
    PluginManifestSmoke
    PluginRegistrationSmoke
    JobGroupLifetimeSmoke
    VolumetricFogSmoke
    InspectorMultiEditSmoke
    MotionVectorGpuAcceptance
    DescriptorPageGrowthSmoke
    TaaGpuAcceptance
    UISliderHoverSmoke
    EditorProgressSmoke
    AssetImportQueueSmoke
    BlenderMeshCacheSmoke
    BlenderPersistenceSmoke
    BlenderCameraSmoke
    StreamlineDx12BridgeSmoke
    D3DDepthStencilSmoke
    PathTracingDenoiserSmoke
    OidnGpuRenderGraphSmoke
    SamplerSmoke)
set(AILU_EXISTING_TEST_TARGETS)
foreach(target_name IN LISTS AILU_TEST_TARGETS)
    if(TARGET ${target_name})
        set_property(TARGET ${target_name} PROPERTY EXCLUDE_FROM_ALL TRUE)
        list(APPEND AILU_EXISTING_TEST_TARGETS ${target_name})
    endif()
endforeach()
add_custom_target(ailu_test_targets DEPENDS ${AILU_EXISTING_TEST_TARGETS})
