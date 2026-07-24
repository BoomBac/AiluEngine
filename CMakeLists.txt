cmake_minimum_required(VERSION 4.1)
project(Ailu)
# 设置项目名称
message("AiluEngine generator is set to ${CMAKE_GENERATOR}")

# 设置编译器标准为C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(AiluBuildPath "${CMAKE_SOURCE_DIR}/.artifacts" CACHE PATH "Root path for built binaries and staged outputs")
file(TO_CMAKE_PATH "${AiluBuildPath}" AiluBuildPath)
message(STATUS "AiluBuildPath is set to ${AiluBuildPath}")
set(AILU_ENGINE_IMPORT_LIB
	$<$<CONFIG:Debug>:${AiluBuildPath}/out/engine/lib/x64/Debug/Engine_d.lib>
	$<$<CONFIG:Release>:${AiluBuildPath}/out/engine/lib/x64/Release/Engine.lib>
	$<$<CONFIG:RelWithDebInfo>:${AiluBuildPath}/out/engine/lib/x64/RelWithDebInfo/Engine.lib>
	$<$<CONFIG:MinSizeRel>:${AiluBuildPath}/out/engine/lib/x64/MinSizeRel/Engine.lib>)
set(AILU_FBXSDK_LINK_LIB
	$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/Engine/Ext/fbxsdk/lib/vs2019/x64/debug/libfbxsdk.lib>
	$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:${CMAKE_SOURCE_DIR}/Engine/Ext/fbxsdk/lib/vs2019/x64/release/libfbxsdk.lib>)

option(AILU_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)
option(AILU_TRACY_ON_DEMAND "Tracy connects only when profiler is attached" ON)
option(AILU_ENABLE_ASSERTS "Enable engine assertions" ON)
option(AILU_ENABLE_ENGINE_PROFILER "Enable built-in profiler" OFF)
option(AILU_ENABLE_D3D_DEBUG_LAYER "Enable D3D12 debug layer" OFF)
option(AILU_ENABLE_PIX_MARKERS "Enable PIX event markers" OFF)

set(AILU_AHT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/AiluHeadTool/aht.ini")
set(AILU_ENGINE_GENERATED_SOURCES)
set(AILU_EDITOR_GENERATED_SOURCES)
set(AILU_REFLECTION_INPUT_HEADERS)
set(AILU_REFLECTION_OUTPUTS)

if(WIN32)
    add_compile_definitions(
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

function(ailu_collect_reflection_outputs root_dir output_var)
	file(GLOB_RECURSE candidate_headers CONFIGURE_DEPENDS "${root_dir}/*.h" "${root_dir}/*.hpp")
	set(local_outputs)
	set(local_inputs)
	foreach(header_file IN LISTS candidate_headers)
		file(STRINGS "${header_file}" generated_include_lines REGEX "#include \"generated/.+\\.gen\\.h\"")
		if(generated_include_lines)
			get_filename_component(header_dir "${header_file}" DIRECTORY)
			get_filename_component(header_name_we "${header_file}" NAME_WE)
			list(APPEND local_inputs "${header_file}")
			list(APPEND local_outputs
				"${header_dir}/generated/${header_name_we}.gen.h"
				"${header_dir}/generated/${header_name_we}.gen.cpp")
		endif()
	endforeach()
	set(${output_var} ${local_outputs} PARENT_SCOPE)
	set(AILU_REFLECTION_INPUT_HEADERS ${AILU_REFLECTION_INPUT_HEADERS} ${local_inputs} PARENT_SCOPE)
	set(AILU_REFLECTION_OUTPUTS ${AILU_REFLECTION_OUTPUTS} ${local_outputs} PARENT_SCOPE)
endfunction()

ailu_collect_reflection_outputs("${CMAKE_SOURCE_DIR}/Engine/Inc" AILU_ENGINE_GENERATED_SOURCES)
ailu_collect_reflection_outputs("${CMAKE_SOURCE_DIR}/Editor/Inc" AILU_EDITOR_GENERATED_SOURCES)
list(REMOVE_DUPLICATES AILU_REFLECTION_INPUT_HEADERS)
list(REMOVE_DUPLICATES AILU_REFLECTION_OUTPUTS)

add_subdirectory("AiluHeadTool")

add_custom_command(
	OUTPUT ${AILU_REFLECTION_OUTPUTS}
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AiluHeadTool>/AHT"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AILU_AHT_CONFIG_FILE}" "$<TARGET_FILE_DIR:AiluHeadTool>/AHT/aht.ini"
	COMMAND ${CMAKE_COMMAND} -E touch "$<TARGET_FILE_DIR:AiluHeadTool>/AHT/cache.txt"
	COMMAND ${CMAKE_COMMAND} -E touch "$<TARGET_FILE_DIR:AiluHeadTool>/AHT/class_ns_map.txt"
	COMMAND $<TARGET_FILE:AiluHeadTool>
	DEPENDS AiluHeadTool ${AILU_REFLECTION_INPUT_HEADERS} "${AILU_AHT_CONFIG_FILE}"
	WORKING_DIRECTORY "$<TARGET_FILE_DIR:AiluHeadTool>"
	COMMENT "Generating reflection code with AiluHeadTool"
	VERBATIM)

add_custom_target(ailu_reflection_codegen DEPENDS ${AILU_REFLECTION_OUTPUTS})

# 直接运行已编译好的 AiluHeadTool.exe（不触发 AiluHeadTool 的编译）
set(AILU_AHT_EXE_DIR "${CMAKE_SOURCE_DIR}/AiluHeadTool/bin/x64" CACHE PATH "Directory containing pre-built AiluHeadTool.exe")
add_custom_target(ailu_reflection_run
	COMMAND ${CMAKE_COMMAND} -E make_directory "${AILU_AHT_EXE_DIR}/$<CONFIG>/AHT"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AILU_AHT_CONFIG_FILE}" "${AILU_AHT_EXE_DIR}/$<CONFIG>/AHT/aht.ini"
	COMMAND ${CMAKE_COMMAND} -E touch "${AILU_AHT_EXE_DIR}/$<CONFIG>/AHT/cache.txt"
	COMMAND ${CMAKE_COMMAND} -E touch "${AILU_AHT_EXE_DIR}/$<CONFIG>/AHT/class_ns_map.txt"
	COMMAND "${AILU_AHT_EXE_DIR}/$<CONFIG>/AiluHeadTool.exe"
	WORKING_DIRECTORY "${AILU_AHT_EXE_DIR}/$<CONFIG>"
	COMMENT "Running AiluHeadTool (pre-built) to generate reflection code"
	VERBATIM)

# 添加engine子目录
add_subdirectory(Engine)

# 添加editor子目录
add_subdirectory(Editor)
add_subdirectory(Player)
add_subdirectory(Test)
