#include "nlohmann/json.hpp"
#include <iostream>
#include <fstream>
#include <array>
#include <vector>
#include <unordered_map>
#include <stdexcept>

// ==================== 终端编码设置（跨平台）====================
#ifdef _WIN32
#include <windows.h>  // Windows 专用头文件
#endif

void initTerminalEncoding() {
#ifdef _WIN32
    // Windows 设置控制台为 UTF-8 编码
    SetConsoleOutputCP(65001);  // 输出编码
    SetConsoleCP(65001);        // 输入编码
#endif
    // Linux/macOS 默认 UTF-8，无需处理
}
// ============================================================

using json = nlohmann::json;

// -------------------- 数据结构定义 --------------------
struct ClassInfo {
    std::array<int, 7> time{};  // 每天的课时分布（位掩码）
    int weeks = 0;              // 周次分布（位掩码）
};

struct CourseInfo {
    int credit = 0;                         // 学分
    std::string name;                       // 课程名
    std::vector<std::string> prereq;        // 先修课程ID列表
    std::unordered_map<std::string, ClassInfo> classes;  // 班级信息
};

struct SelInfo {
    int semester = -1;         // 选课学期（-1表示未选）
    std::string class_id;      // 班级ID
};

// -------------------- 工具函数 --------------------
json read_json(const std::string& file) {
    std::ifstream fin(file);
    if (!fin)
        throw std::runtime_error("无法打开 " + file);
    json j;
    fin >> j;
    return j;
}

static std::string extract_name(const json& c) {
    if (c.contains("course_name"))
        return c["course_name"];
    if (c.contains("name"))
        return c["name"];
    return "";
}

static std::string wrap(
    const std::string& id,
    const std::unordered_map<std::string, CourseInfo>& mp
) {
    auto it = mp.find(id);
    if (it != mp.end() && !it->second.name.empty())
        return id + "（" + it->second.name + "）";
    return id;
}

static bool overlap(const ClassInfo& a, const ClassInfo& b) {
    if ((a.weeks & b.weeks) == 0)
        return false;
    for (int d = 0; d < 7; ++d)
        if (a.time[d] & b.time[d])
            return true;
    return false;
}

// -------------------- 主逻辑 --------------------
int main() {
    // 初始化终端编码（解决中文乱码）
    initTerminalEncoding();

    try {
        // 1. 读取课程数据
        const json jc = read_json("course.json");
        std::unordered_map<std::string, CourseInfo> courseMap;

        for (const auto& c : jc) {
            std::string cid;
            if (c.contains("course_id"))
                cid = c["course_id"];
            else if (c.contains("id"))
                cid = c["id"];
            else
                continue;

            CourseInfo info;
            if (c.contains("credit"))
                info.credit = c["credit"].get<int>();
            info.name = extract_name(c);

            if (c.contains("prerequisites"))
                info.prereq = c["prerequisites"].get<std::vector<std::string>>();

            if (c.contains("offerings"))
                for (const auto& o : c["offerings"]) {
                    std::string cls;
                    if (o.contains("class_id"))
                        cls = o["class_id"];
                    else if (o.contains("id"))
                        cls = o["id"];
                    if (cls.empty())
                        continue;

                    ClassInfo ci;
                    if (o.contains("times")) {
                        const auto& arr = o["times"];
                        for (int d = 0; d < 7 && d < arr.size(); ++d)
                            ci.time[d] = arr[d].get<int>();
                    }
                    if (o.contains("weeks"))
                        ci.weeks = o["weeks"].get<int>();
                    info.classes.emplace(cls, std::move(ci));
                }
            courseMap.emplace(cid, std::move(info));
        }

        // 2. 读取选课方案
        const json js = read_json("schedule.json");
        std::unordered_map<std::string, SelInfo> selMap;
        for (const auto& e : js) {
            std::string cid;
            if (e.contains("course_id"))
                cid = e["course_id"];
            else if (e.contains("id"))
                cid = e["id"];
            else
                continue;

            SelInfo s;
            if (e.contains("semester"))
                s.semester = e["semester"];
            if (e.contains("class_id"))
                s.class_id = e["class_id"];
            else if (e.contains("class"))
                s.class_id = e["class"];
            selMap[cid] = std::move(s);
        }

        // 3. 检查选课合法性
        bool allOK = true;
        std::vector<std::string> errs;
        int totalCredits = 0;

        // 3.1 检查课程是否存在
        for (const auto& [cid, s] : selMap) {
            if (s.semester < 0)
                continue;
            auto it = courseMap.find(cid);
            if (it == courseMap.end()) {
                errs.emplace_back("课程 " + cid + " 不存在于 course.json");
                allOK = false;
                continue;
            }
            if (it->second.classes.count(s.class_id) == 0) {
                errs.emplace_back("课程 " + wrap(cid, courseMap) +
                                 " 的班号 " + s.class_id + " 不在 offerings 中");
                allOK = false;
            }
        }

        // 3.2 检查先修课程
        for (const auto& [cid, s] : selMap) {
            if (s.semester < 0)
                continue;
            const auto& course = courseMap.at(cid);
            for (const std::string& pre : course.prereq) {
                auto it = selMap.find(pre);
                if (it == selMap.end() || it->second.semester < 0) {
                    errs.emplace_back("课程 " + wrap(cid, courseMap) +
                                     " 缺少先修课 " + wrap(pre, courseMap));
                    allOK = false;
                }
                else if (it->second.semester >= s.semester) {
                    errs.emplace_back("课程 " + wrap(cid, courseMap) +
                                     " 的先修课 " + wrap(pre, courseMap) +
                                     " 学期 " + std::to_string(it->second.semester) +
                                     " 需早于本课学期 " + std::to_string(s.semester));
                    allOK = false;
                }
            }
        }

        // 3.3 检查时间冲突
        std::unordered_map<int, std::vector<std::pair<std::string, const ClassInfo*>>> semTable;
        for (const auto& [cid, s] : selMap) {
            if (s.semester < 0)
                continue;
            const auto& cinfo = courseMap.at(cid);
            auto itCls = cinfo.classes.find(s.class_id);
            if (itCls == cinfo.classes.end())
                continue;
            semTable[s.semester].push_back({cid, &itCls->second});
        }

        for (const auto& [sem, vec] : semTable) {
            for (size_t i = 0; i < vec.size(); ++i)
                for (size_t j = i + 1; j < vec.size(); ++j) {
                    if (overlap(*vec[i].second, *vec[j].second)) {
                        errs.emplace_back("学期 " + std::to_string(sem) + " 内 " +
                                         wrap(vec[i].first, courseMap) + " 与 " +
                                         wrap(vec[j].first, courseMap) + " 时间冲突");
                        allOK = false;
                    }
                }
        }

        // 4. 统计总学分
        for (const auto& [cid, s] : selMap)
            if (s.semester >= 0) {
                auto it = courseMap.find(cid);
                if (it != courseMap.end())
                    totalCredits += it->second.credit;
            }

        // 5. 输出结果
        if (allOK)
            std::cout << "✔ 选课方案合法\n";
        else {
            std::cout << "✘ 发现以下问题：\n";
            for (const auto& e : errs)
                std::cout << "  - " << e << '\n';
        }
        std::cout << "总学分: " << totalCredits << '\n';
        return allOK ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "运行时错误: " << e.what() << '\n';
        return 1;
    }
}
