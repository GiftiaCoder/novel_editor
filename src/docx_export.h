#pragma once

#include "common.h"

struct ChapterSortKey {
    bool has_number = false;
    double number = 0.0;
};

ChapterSortKey extract_chapter_sort_key(const std::string& name);
std::vector<std::string> sort_chapters_for_export(std::vector<std::string> chapters, const std::string& order);
std::string xml_escape(const std::string& input);
std::vector<std::string> split_lines_preserve_empty(const std::string& text);
std::string make_docx_paragraph(const std::string& text, bool chapter_title = false);
std::string make_docx_novel_title(const std::string& text);
std::string build_document_xml(const std::vector<std::string>& chapters, const std::string& type, const std::string& novel_name);
uint32_t crc32_calc(const std::string& data);
void append_u16(std::string& out, uint16_t v);
void append_u32(std::string& out, uint32_t v);
std::string build_zip_store(const std::vector<std::pair<std::string, std::string>>& files);
std::string build_export_docx(const std::vector<std::string>& chapters, const std::string& type, const std::string& novel_name);
std::string current_export_timestamp_string();
std::string sanitize_ascii_filename(const std::string& value);
std::string percent_encode_utf8(const std::string& value);
std::string make_export_filename_base();
