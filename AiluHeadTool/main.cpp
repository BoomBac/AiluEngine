#include "AiluHeadTool.h"
#include "Timer.h"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <array>

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

uint32_t murmur3_32(const uint8_t *key, size_t len, uint32_t seed = 0)
{
    uint32_t h = seed;
    if (len > 3)
    {
        const uint32_t *key_x4 = (const uint32_t *) key;
        size_t i = len >> 2;
        do {
            uint32_t k = *key_x4++;
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64;
        } while (--i);
        key = (const uint8_t *) key_x4;
    }
    if (len & 3)
    {
        size_t i = len & 3;
        uint32_t k = 0;
        key = &key[i - 1];
        do {
            k <<= 8;
            k |= *key--;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    h *= 0xc2b2ae35;
    h ^= (h >> 16);
    return h;
}

uint32_t calc_file_murmur3(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed to open file: " + path.string());

    std::array<uint8_t, 8192> buffer;
    uint32_t hash = 0;
    while (file)
    {
        file.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        auto bytes = file.gcount();
        if (bytes > 0)
            hash ^= murmur3_32(buffer.data(), bytes, hash);
    }
    return hash;
}

int main(int argc, char **argv)
{
    bool is_debug = false, is_rebuild_all = false;
    for (int i = 1; i < argc; ++i)
    {
        if (string(argv[i]) == "-debug")
        {
            is_debug = true;
        }
        else if (string(argv[i]) == "-force")
        {
            is_rebuild_all = true;
        }
        else if (string(argv[i]) == "-help")
        {
            std::cout << "AiluHeadTool -debug" << std::endl;
            std::cout << "AiluHeadTool -help" << std::endl;
            std::cout << "AiluHeadTool -force:  re-generate all reflect info" << std::endl;
            return 0;
        }
    }
    Timer timer;
    timer.start();
    std::cout << "AiluHeadTool start..." << std::endl;
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
    auto class_ns_path = tool_dir / "class_ns_map.txt";
    Config config;
    LoadConfig(config_dir, config);
    map<string, uint32_t> cache_file_hashes;
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
                string file_hash = line.substr(pos + 1);
                cache_file_hashes[path] = std::stoll(file_hash);
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
    fs::path proj_dir = tool_dir.parent_path().parent_path().parent_path().parent_path().parent_path().parent_path();
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
    map<string, uint32_t> current_file_hashes;
    for (auto &inc_file: all_inc_sys_path)
    {
        all_inc_rela_path.push_back(fs::relative(inc_file, proj_dir));
        if (is_debug)
            std::cout << all_inc_rela_path.back().string() << std::endl;
        current_file_hashes[all_inc_rela_path.back().string()] = calc_file_murmur3(inc_file);
    }
    set<fs::path> work_files;
    set<fs::path> class_ns_update_files;
    //is_rebuild_all = true;
    //std::cout << "force rebuild all..." << std::endl;
    for (auto &p: all_inc_rela_path)
    {
        auto sys_path = proj_dir / p;
        uint32_t file_hash = calc_file_murmur3(sys_path);
        if (cache_file_hashes.contains(p.string()))
        {
            if (current_file_hashes[p.string()] != cache_file_hashes[p.string()])
            {
                cout << "File changed: " << p.string() << endl;
                work_files.insert(sys_path);
                class_ns_update_files.insert(sys_path);
            }
        }
        else
        {
            work_files.insert(sys_path);
            class_ns_update_files.insert(sys_path);
            cout << "New file: " << p.string() << endl;
        }
        cache_file_hashes[p.string()] = file_hash;
        if (is_rebuild_all)
            work_files.insert(sys_path);
    }
    ofstream cache_file(cache_dir.string());
    for (auto &p: cache_file_hashes)
    {
        cache_file << p.first << "," << p.second << std::endl;
    }
    cache_file.close();
    AiluHeadTool::AddDependencyInc("<memory>");
    AiluHeadTool::AddDependencyInc("<Objects/Type.h>");
    AiluHeadTool::AddDependencyInc("<Objects/Serialize.h>");
    AiluHeadTool::AddDependencyInc("<Objects/SerializeSpecializations.h>");
    AiluHeadTool::AddDependencyInc("<Framework/Common/Log.h>");
    AiluHeadTool aht;
    std::cout << "ColloctClassNamespace..." << std::endl;
    aht.ColloctClassNamespace(class_ns_update_files, class_ns_path);
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
    return 0;
    //system("pause");
}