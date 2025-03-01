project (AiluHeadTool)
set(AHT_ROOT_PATH ${CMAKE_SOURCE_DIR}/AiluHeadTool)
set(ENGINE_ROOT_PATH ${CMAKE_SOURCE_DIR}/Engine)
set(ENGINE_LIB_ROOT ${ENGINE_ROOT_PATH}/lib/x64)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${AHT_ROOT_PATH}/bin/x64)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${AHT_ROOT_PATH}/bin/x64/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${AHT_ROOT_PATH}/bin/x64/release)

#set(ENGINE_LIB_ROOT ${ENGINE_ROOT_PATH}/lib/x64)
# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AiluHeadTool "AiluHeadTool.h" "AiluHeadTool.cpp" "main.cpp"
        Timer.h)

# 包含路径
#target_include_directories(AiluHeadTool PUBLIC ${AHT_ROOT_PATH}/)
#target_include_directories(AiluHeadTool PUBLIC ${AHT_ROOT_PATH}/Inc)
target_include_directories(AiluHeadTool PUBLIC ${ENGINE_ROOT_PATH})
target_include_directories(AiluHeadTool PUBLIC ${ENGINE_ROOT_PATH}/Inc)
# target_include_directories(Test PUBLIC ${ENGINE_ROOT_PATH}/Ext)

# 宏定义
target_compile_definitions(AiluHeadTool PUBLIC _DEBUG)
set_target_properties (AiluHeadTool PROPERTIES OUTPUT_NAME "AiluHeadTool")
# 编译器参数
# if(MSVC)
target_compile_options(AiluHeadTool PUBLIC /MP)
target_compile_options(AiluHeadTool PUBLIC /D_UNICODE /DUNICODE /wd4819) # /wd4100 禁用未使用的参数警告
#target_link_libraries(AiluHeadTool PRIVATE
#        $<$<CONFIG:Debug>:${ENGINE_LIB_ROOT}/Debug/Engine_d.lib>
#        $<$<CONFIG:Release>:${ENGINE_LIB_ROOT}/Release/Engine.lib>)