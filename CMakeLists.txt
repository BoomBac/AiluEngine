cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(Ailu)

# 设置编译器标准为C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 添加engine子目录
add_subdirectory(Engine)

# 添加editor子目录
add_subdirectory(Editor)
add_subdirectory(Test)