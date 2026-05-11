#pragma once

#include "common.h"

std::string read_latest_content(const std::string& chapter, const std::string& type);
std::vector<std::string> get_chapter_list();
std::string get_safe_preview(const std::string& content, int max_length = 100);
std::string save_version_file(const std::string& chapter, const std::string& type, const std::string& content);
bool rename_chapter(const std::string& old_name, const std::string& new_name);
bool delete_chapter(const std::string& chapter);
bool create_chapter(const std::string& chapter);
