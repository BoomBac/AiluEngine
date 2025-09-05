#include <algorithm>
#include <regex>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

class NamespaceTracker
{
public:
    // 每次处理一行，返回当前命名空间（例如：A::B）
    std::string ProcessLine(const std::string &line)
    {
        std::smatch match;

        // 移除注释
        std::string clean_line = RemoveLineComment(line);

        // 匹配 namespace A::B 或 namespace A::B::C {
        if (std::regex_search(clean_line, match, std::regex(R"(\bnamespace\s+([a-zA-Z_][a-zA-Z0-9_:]*)\s*\{?)")))
        {
            std::string full_ns = match[1].str();
            std::vector<std::string> segments = Split(full_ns, "::");

            // 清理分割后的空段
            segments.erase(
                std::remove_if(segments.begin(), segments.end(),
                    [](const std::string& s) { return s.empty(); }),
                segments.end()
            );

            for (const auto &s: segments)
            {
                _stack.emplace_back(Frame{.name = s, .is_namespace = true});
            }

            // 如果没带 {，需要等后续行补 {
            if (clean_line.find('{') == std::string::npos)
            {
                _waiting_brace += 1; // 只需要等待一个 { 因为C++17嵌套命名空间只需要一个大括号
            }

            return GetCurrentNamespace();
        }

        // 处理 { 和 }
        for (char c: clean_line)
        {
            if (c == '{')
            {
                if (_waiting_brace > 0)
                {
                    _waiting_brace--;
                }
                else
                {
                    // 非命名空间的大括号
                    _stack.emplace_back(Frame{.name = "", .is_namespace = false});
                }
            }
            else if (c == '}')
            {
                if (!_stack.empty())
                {
                    //// 如果是命名空间的结束，可能需要弹出多个命名空间
                    //if (!_stack.empty() && _stack.back().is_namespace)
                    //{
                    //    // 找到最近的命名空间块的开始
                    //    size_t ns_count = 0;
                    //    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it)
                    //    {
                    //        if (it->is_namespace)
                    //            ns_count++;
                    //        else
                    //            break;
                    //    }

                    //    // 弹出整个命名空间块
                    //    for (size_t i = 0; i < ns_count && !_stack.empty(); ++i)
                    //    {
                    //        _stack.pop_back();
                    //    }
                    //}
                    //else
                    {
                        // 普通大括号，弹出一个
                        _stack.pop_back();
                    }
                }
            }
        }

        return GetCurrentNamespace();
    }

    /// @brief 清空内部状态，处理新文件时调用
    void Clean()
    {
        _stack.clear();
        _waiting_brace = 0;
    }

    /// @brief 获取当前命名空间的深度
    size_t GetNamespaceDepth() const
    {
        size_t depth = 0;
        for (const auto &f: _stack)
        {
            if (f.is_namespace)
                depth++;
        }
        return depth;
    }
private:
    struct Frame
    {
        std::string name;
        bool is_namespace;
    };

    std::vector<Frame> _stack;
    int _waiting_brace = 0;

    std::string GetCurrentNamespace() const
    {
        std::ostringstream oss;
        bool first = true;
        for (const auto &f: _stack)
        {
            if (f.is_namespace && !f.name.empty())
            {
                if (!first) oss << "::";
                oss << f.name;
                first = false;
            }
        }
        return oss.str();
    }

    static std::vector<std::string> Split(const std::string &str, const std::string &delim)
    {
        std::vector<std::string> tokens;
        size_t start = 0, end;
        while ((end = str.find(delim, start)) != std::string::npos)
        {
            tokens.push_back(str.substr(start, end - start));
            start = end + delim.length();
        }
        tokens.push_back(str.substr(start));
        return tokens;
    }

    static std::string RemoveLineComment(const std::string &line)
    {
        size_t pos = line.find("//");
        if (pos != std::string::npos)
            return line.substr(0, pos);
        return line;
    }
};
