#include "utils.h"

// 工具函数：生成UUID
std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            ss << "-";
        } else if (i == 14) {
            ss << "4";
        } else if (i == 19) {
            ss << std::hex << dis2(gen);
        } else {
            ss << std::hex << dis(gen);
        }
    }
    return ss.str();
}

// 工具函数：获取当前时间戳
std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

// 确保目录存在
bool ensure_directory(const fs::path& path) {
    if (!fs::exists(path)) {
        return fs::create_directories(path);
    }
    return true;
}

// 安全读取文件内容（处理编码问题）
std::string safe_read_file(const fs::path& file_path) {
    if (!fs::exists(file_path)) {
        return "";
    }
    
    try {
        // 以二进制模式读取
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        
        // 读取整个文件
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();
        
        // 验证是否为有效的UTF-8，如果不是则清理
        std::string clean_content;
        for (unsigned char c : content) {
            // 保留有效的ASCII字符和UTF-8序列
            if (c == 0x09 || c == 0x0A || c == 0x0D || (c >= 0x20 && c <= 0x7E)) {
                // 有效的ASCII字符（包括制表符、换行、回车）
                clean_content += c;
            } else if (c >= 0x80) {
                // UTF-8 多字节字符，保留
                clean_content += c;
            }
            // 忽略其他控制字符
        }
        
        return clean_content;
    } catch (const std::exception& e) {
        std::cerr << "Error reading file: " << e.what() << std::endl;
        return "";
    }
}

// 安全写入文件
bool safe_write_file(const fs::path& file_path, const std::string& content) {
    try {
        std::ofstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        file.write(content.c_str(), content.length());
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error writing file: " << e.what() << std::endl;
        return false;
    }
}
