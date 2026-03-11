cmake_minimum_required(VERSION 4.1)
project(Ailu)
# 设置项目名称
message("AiluEngine generator is set to ${CMAKE_GENERATOR}")

# 设置编译器标准为C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(AiluBuildPath F://AiluBuild)
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

# 添加engine子目录
add_subdirectory(Engine)

# 添加editor子目录
add_subdirectory(Editor)
add_subdirectory(Test)
add_subdirectory("AiluHeadTool")
