//// AiluHeadTool.cpp : Source file for your target.
////
//#include <iostream>
//#include <fstream>
//#include <string>
//#include <vector>
//#include <regex>
//#include "AiluHeadTool.h"
//#include "TypeInfo.h"
//
//int main()
//{
//    std::ifstream file("Test.h");
//    if (file.is_open())
//    {
//        std::string str;
//        std::vector<std::string> v;
//        std::vector<std::string> reflect_class_name;
//        std::vector<std::string> reflect_prop_name;
//        std::vector<std::string> reflect_func_name;
//        std::string last_class;
//        bool has_property = false;
//        std::string class_id("class");
//        std::string aclass_id("ACLASS");
//        std::string prop_id("PROPERTY");
//        std::string func_id("FUNCTION");
//        std::regex prop_pattern(R"(\b(\w+)\b\s+(\w+))");
//        std::vector<std::tuple<std::string, std::string>> prop_pairs;
//        while (std::getline(file, str))
//        {
//            v.emplace_back(str);
//            if (has_property)
//            {
//                std::smatch match;
//                if (std::regex_search(str, match,prop_pattern))
//                {
//                    std::string type = match[1].str();
//                    std::string identifier = match[2].str();
//                    reflect_prop_name.emplace_back(identifier);
//                    prop_pairs.emplace_back(type, identifier);
//                    has_property = false;
//                    continue;
//                }
//            }
//            if (str.find(class_id) != std::string::npos)
//                last_class = str;
//            else if (str.find(aclass_id) != std::string::npos)
//            {
//                reflect_class_name.emplace_back(last_class.substr(last_class.find_last_of(" ") + 1));
//                last_class.clear();
//            }
//            else if (str.find(prop_id) != std::string::npos)
//            {
//                has_property = true;
//            }
//        }
//        //print reflect_class_name
//        for (auto& cname : reflect_class_name)
//            std::cout << cname << std::endl;
//        for (auto &p: prop_pairs)
//        {
//            auto& [t, i] = p;
//            std::cout << t << " " << i << std::endl;
//        }
//    }
//    else
//    {
//        std::cout << "open file failed" << std::endl;
//    }
//
//	std::cout << "Hello World!\n";
//	return 0;
//}
