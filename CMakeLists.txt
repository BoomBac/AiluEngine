cmake_minimum_required(VERSION 4.1)
project(Ailu)
# 设置项目名称
message("AiluEngine generator is set to ${CMAKE_GENERATOR}")

# 设置编译器标准为C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(AiluBuildPath F://AiluBuild)

# 添加engine子目录
add_subdirectory(Engine)

# 添加editor子目录
add_subdirectory(Editor)
add_subdirectory(Test)
add_subdirectory("AiluHeadTool")
