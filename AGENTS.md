# Repository Instructions

1. 代码风格：变量名使用小写和下划线组合；类/结构体成员变量以下划线开头，之后使用小写和下划线组合；静态变量以 `s` 开头；常量以小写 `k` 开头，之后使用驼峰命名；类/结构体名以及函数名称使用驼峰命名。
2. 代码按 120 列格式化，减少换行。
3. 构建验证只能使用 debug preset：`cmake --build --preset build-debug`。不要使用 `cmake --build --preset build-develop`。
