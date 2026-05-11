#pragma once

#include "common.h"

std::string generate_uuid();
std::string get_current_timestamp();
bool ensure_directory(const fs::path& path);
std::string safe_read_file(const fs::path& file_path);
bool safe_write_file(const fs::path& file_path, const std::string& content);
