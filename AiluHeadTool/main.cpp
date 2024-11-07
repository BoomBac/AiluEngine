#include "AiluHeadTool.h"
#include "Timer.h"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace std;
struct Config
{
    vector<fs::path> _head_dir;
};

void LoadConfig(const fs::path &p, Config &config)
{
    map<string, vector<string>> block;
    bool is_block_start = false, is_block_end = false;
    if (fs::exists(p))
    {
        std::ifstream file(p.string());
        std::string cur_group;
        if (file.is_open())
        {
            std::string line;
            while (std::getline(file, line))
            {
                if (line[0] == '[' && line[line.size() - 1] == ']')
                {
                    if (!is_block_start)
                    {
                        is_block_start = true;
                        cur_group = line.substr(1, line.size() - 2);
                        block[cur_group] = vector<string>();
                    }
                    else
                    {
                        is_block_end = true;
                        is_block_start = false;
                    }
                }
                else if (is_block_start)
                {
                    block[cur_group].push_back(line);
                }
            }
            file.close();
        }
        else
        {
            std::cerr << "AHT: Unable to open config file: " << p << std::endl;
        }
    }
    else
    {
        std::cerr << "AHT: Config file not found: " << p << std::endl;
    }
    for (auto &ps: block["HeadDir"])
        config._head_dir.emplace_back(ps);
}
namespace fs = std::filesystem;

//填充监听路径下的所有文件
void TraverseDirectory(const fs::path &directoryPath, set<fs::path> &path_set)
{
    for (const auto &entry: fs::directory_iterator(directoryPath))
    {
        if (fs::is_directory(entry.status()))
        {
            TraverseDirectory(entry.path(), path_set);
        }
        else if (fs::is_regular_file(entry.status()))
        {
            if (entry.path().string().find("gen.") == string::npos)
                path_set.insert(entry.path());
        }
    }
}
// 将字符串转换为 time_point
std::chrono::system_clock::time_point StringToTimePoint(const std::string &timeStr)
{
    std::tm tm = {};
    std::istringstream ss(timeStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail())
    {
        throw std::runtime_error("Failed to parse time string");
    }
    auto timeT = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(timeT);
}

int main(int argc, char **argv)
{
    Timer timer;
    timer.start();
    std::cout << "AiluHeadTool start..." << std::endl;
    bool is_debug = false;
    if (argc > 1 && string(argv[1]) == "-debug")
    {
        is_debug = true;
    }
    string work_path_str = argv[0];
    fs::path work_path;
    if (work_path_str.empty())
    {
        work_path = fs::current_path();
        if (is_debug)
            std::cout << "haven't work path input use " << fs::current_path() << " instead" << work_path.string() << std::endl;
    }
    else
    {
        if (is_debug)
            std::cout << "input work path: " << work_path_str << std::endl;
        work_path = fs::path(work_path_str).parent_path();
    }
    auto tool_dir = work_path / "AHT/";
    auto config_dir = tool_dir / "aht.ini";
    auto cache_dir = tool_dir / "cache.txt";
    Config config;
    LoadConfig(config_dir, config);
    map<string, long long> cache_file_times;
    if (fs::exists(cache_dir))
    {
        std::ifstream file(cache_dir.string());
        std::string line;
        while (std::getline(file, line))
        {
            auto pos = line.find(',');
            if (pos != std::string::npos)
            {
                string path = line.substr(0, pos);
                string last_write_time = line.substr(pos + 1);
                cache_file_times[path] = std::stoll(last_write_time);
            }
        }
        file.close();
    }
    else
    {
        std::cout << "cache_dir: " << cache_dir.string() << "not exist" << std::endl;
        fs::create_directories(cache_dir.parent_path());
    }
    //..../AiluEngine/
    fs::path proj_dir = tool_dir.parent_path().parent_path().parent_path().parent_path().parent_path();
    if (is_debug)
    {
        std::cout << "tool_dir: " << tool_dir.string() << std::endl;
        std::cout << "proj_dir: " << proj_dir.string() << std::endl;
        std::cout << "config._head_dir: " << config._head_dir.size() << std::endl;
    }
    for (auto &dir: config._head_dir)
    {
        dir = proj_dir / dir;
    }
    set<fs::path> all_inc_sys_path;
    for (auto &dir: config._head_dir)
    {
        TraverseDirectory(dir, all_inc_sys_path);
    }
    vector<fs::path> all_inc_rela_path;
    map<string, long long> current_file_times;
    for (auto &inc_file: all_inc_sys_path)
    {
        all_inc_rela_path.push_back(fs::relative(inc_file, proj_dir));
        if (is_debug)
            std::cout << all_inc_rela_path.back().string() << std::endl;
        current_file_times[all_inc_rela_path.back().string()] = fs::last_write_time(inc_file).time_since_epoch().count();
    }
    set<fs::path> work_files;
    for (auto &p: all_inc_rela_path)
    {
        auto sys_path = proj_dir / p;
        auto cur_time = fs::last_write_time(sys_path).time_since_epoch().count();
        if (cache_file_times.contains(p.string()))
        {
            if (cur_time > cache_file_times[p.string()])
            {
                cout << "File changed: " << p.string() << endl;
                work_files.insert(sys_path);
            }
        }
        else
        {
            work_files.insert(sys_path);
            cout << "New file: " << p.string() << endl;
        }
        cache_file_times[p.string()] = cur_time;
    }
    ofstream cache_file(cache_dir.string());
    for (auto &p: cache_file_times)
    {
        cache_file << p.first << "," << p.second << std::endl;
    }
    cache_file.close();
    AiluHeadTool::AddDependencyInc("<memory>");
    AiluHeadTool::AddDependencyInc("<Inc/Objects/Type.h>");
    AiluHeadTool aht;
    for (auto &p: work_files)
    {
        auto out_dir = p.parent_path() / "generated";
        if (!fs::exists(out_dir))
            fs::create_directories(out_dir);
        aht.Parser(p, out_dir);
    }
    aht.SaveLog(tool_dir);
    timer.stop();
    cout << "AiluHeadTool end after " << timer.ElapsedSeconds() << " sec..." << endl;
    //system("pause");
}