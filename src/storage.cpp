#include "storage.h"
#include "config.h"
#include "utils.h"

// 读取最新版本内容
std::string read_latest_content(const std::string& chapter, const std::string& type) {
    fs::path content_path = fs::path(FLAGS_content_root) / "latest" / chapter / type;
    
    // 如果直接文件存在
    if (fs::exists(content_path) && fs::is_regular_file(content_path)) {
        return safe_read_file(content_path);
    }
    
    // 如果是软链接
    if (fs::exists(content_path) && fs::is_symlink(content_path)) {
        try {
            fs::path target = fs::read_symlink(content_path);
            fs::path absolute_target = fs::path(FLAGS_content_root) / "latest" / chapter / target;
            if (fs::exists(absolute_target)) {
                return safe_read_file(absolute_target);
            }
        } catch (...) {
            return "";
        }
    }
    
    return "";
}

// 获取所有章节列表
std::vector<std::string> get_chapter_list() {
    std::vector<std::string> chapters;
    fs::path latest_dir = fs::path(FLAGS_content_root) / "latest";
    
    if (!fs::exists(latest_dir)) {
        return chapters;
    }
    
    for (const auto& entry : fs::directory_iterator(latest_dir)) {
        if (fs::is_directory(entry.path())) {
            std::string chapter_name = entry.path().filename().string();
            chapters.push_back(chapter_name);
        }
    }
    
    std::sort(chapters.begin(), chapters.end());
    return chapters;
}

// 获取文本预览（安全版本）
std::string get_safe_preview(const std::string& content, int max_length) {
    if (content.empty()) {
        return "";
    }
    
    // 确保不会截断UTF-8字符
    if (content.length() <= max_length) {
        return content;
    }
    
    // 找到安全的截断点
    int cut_pos = max_length;
    while (cut_pos > 0 && (static_cast<unsigned char>(content[cut_pos]) & 0xC0) == 0x80) {
        cut_pos--;
    }
    
    return content.substr(0, cut_pos) + "...";
}

// 保存版本文件
std::string save_version_file(const std::string& chapter, const std::string& type, 
                               const std::string& content) {
    // 清理内容，确保是有效的UTF-8
    std::string clean_content = content;
    // 移除无效的UTF-8序列（简单清理）
    std::string valid_content;
    for (size_t i = 0; i < clean_content.length(); ) {
        unsigned char c = clean_content[i];
        if (c < 0x80) {
            // ASCII字符
            valid_content += c;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2字节UTF-8
            if (i + 1 < clean_content.length()) {
                valid_content += clean_content.substr(i, 2);
                i += 2;
            } else {
                i++;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3字节UTF-8
            if (i + 2 < clean_content.length()) {
                valid_content += clean_content.substr(i, 3);
                i += 3;
            } else {
                i++;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4字节UTF-8
            if (i + 3 < clean_content.length()) {
                valid_content += clean_content.substr(i, 4);
                i += 4;
            } else {
                i++;
            }
        } else {
            // 无效字符，跳过
            i++;
        }
    }
    
    // 生成时间戳和UUID
    std::string timestamp = get_current_timestamp();
    std::string uuid = generate_uuid();
    
    // 构建版本文件路径
    fs::path version_dir = fs::path(FLAGS_content_root) / "versions" / chapter;
    ensure_directory(version_dir);
    
    std::string filename = type + "_" + timestamp + "_" + uuid;
    fs::path file_path = version_dir / filename;
    
    // 写入文件
    if (!safe_write_file(file_path, valid_content)) {
        throw std::runtime_error("Cannot create version file: " + file_path.string());
    }
    
    // 更新latest目录
    fs::path latest_dir = fs::path(FLAGS_content_root) / "latest" / chapter;
    ensure_directory(latest_dir);
    
    fs::path latest_file = latest_dir / type;
    
    // 写入latest文件
    if (!safe_write_file(latest_file, valid_content)) {
        throw std::runtime_error("Cannot write latest file: " + latest_file.string());
    }
    
    return file_path.string();
}

// 重命名章节（修复软链接）
bool rename_chapter(const std::string& old_name, const std::string& new_name) {
    // 检查新章节名合法性
    if (new_name.find('/') != std::string::npos || 
        new_name.find('\\') != std::string::npos ||
        new_name.empty()) {
        return false;
    }
    
    fs::path old_version_dir = fs::path(FLAGS_content_root) / "versions" / old_name;
    fs::path old_latest_dir = fs::path(FLAGS_content_root) / "latest" / old_name;
    fs::path new_version_dir = fs::path(FLAGS_content_root) / "versions" / new_name;
    fs::path new_latest_dir = fs::path(FLAGS_content_root) / "latest" / new_name;
    
    // 检查新目录是否已存在
    if (fs::exists(new_version_dir) || fs::exists(new_latest_dir)) {
        return false;
    }
    
    // 备份旧的软链接目标路径（相对路径）
    std::string outline_target, body_target;
    auto read_link_target = [&](const fs::path& link_path) -> std::string {
        if (fs::exists(link_path) && fs::is_symlink(link_path)) {
            try {
                return fs::read_symlink(link_path).string();
            } catch (...) {
                return "";
            }
        }
        return "";
    };
    
    fs::path old_outline_link = old_latest_dir / "outline";
    fs::path old_body_link = old_latest_dir / "body";
    outline_target = read_link_target(old_outline_link);
    body_target = read_link_target(old_body_link);
    
    // 重命名目录
    bool success = true;
    if (fs::exists(old_version_dir)) {
        try {
            fs::rename(old_version_dir, new_version_dir);
        } catch (...) {
            success = false;
        }
    }
    if (fs::exists(old_latest_dir) && success) {
        try {
            fs::rename(old_latest_dir, new_latest_dir);
        } catch (...) {
            // 回滚 version 目录
            if (fs::exists(new_version_dir)) {
                fs::rename(new_version_dir, old_version_dir);
            }
            success = false;
        }
    }
    
    if (!success) return false;
    
    // 重新创建软链接（因为旧链接的目标路径中包含旧章节名，需要更新）
    auto recreate_link = [&](const std::string& type, const std::string& old_target) {
        if (old_target.empty()) return;
        
        // 旧目标路径是相对路径，例如 "../../versions/old_name/outline_xxx"
        // 需要将其中的 "old_name" 替换为 "new_name"
        std::string new_target = old_target;
        size_t pos = new_target.find(old_name);
        if (pos != std::string::npos) {
            new_target.replace(pos, old_name.length(), new_name);
        }
        
        fs::path link_path = new_latest_dir / type;
        // 删除旧的链接（如果存在且为软链接）
        if (fs::exists(link_path)) {
            fs::remove(link_path);
        }
        // 创建新链接
        try {
            fs::create_symlink(new_target, link_path);
        } catch (...) {
            // 如果相对路径无效，尝试基于新版本目录中的最新文件创建
            // 查找新版本目录下该类型的最新文件
            fs::path version_dir = new_version_dir;
            if (fs::exists(version_dir)) {
                std::string prefix = type + "_";
                fs::path latest_file;
                for (const auto& entry : fs::directory_iterator(version_dir)) {
                    std::string filename = entry.path().filename().string();
                    if (filename.find(prefix) == 0) {
                        if (latest_file.empty() || filename > latest_file.filename().string()) {
                            latest_file = entry.path();
                        }
                    }
                }
                if (!latest_file.empty()) {
                    fs::path relative = fs::relative(latest_file, new_latest_dir);
                    fs::create_symlink(relative, link_path);
                }
            }
        }
    };
    
    recreate_link("outline", outline_target);
    recreate_link("body", body_target);
    
    return true;
}

// 删除章节
bool delete_chapter(const std::string& chapter) {
    fs::path version_dir = fs::path(FLAGS_content_root) / "versions" / chapter;
    fs::path latest_dir = fs::path(FLAGS_content_root) / "latest" / chapter;
    
    bool success = true;
    
    if (fs::exists(version_dir)) {
        try {
            fs::remove_all(version_dir);
        } catch (...) {
            success = false;
        }
    }
    
    if (fs::exists(latest_dir)) {
        try {
            fs::remove_all(latest_dir);
        } catch (...) {
            success = false;
        }
    }
    
    return success;
}

// 创建新章节
bool create_chapter(const std::string& chapter) {
    if (chapter.find('/') != std::string::npos || 
        chapter.find('\\') != std::string::npos ||
        chapter.empty()) {
        return false;
    }
    
    fs::path latest_dir = fs::path(FLAGS_content_root) / "latest" / chapter;
    if (fs::exists(latest_dir)) {
        return false;
    }
    
    fs::path version_dir = fs::path(FLAGS_content_root) / "versions" / chapter;
    
    return ensure_directory(version_dir) && ensure_directory(latest_dir);
}
