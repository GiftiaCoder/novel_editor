#include "docx_export.h"
#include "config.h"
#include "storage.h"

// ---------- DOCX 导出：按章节名 + 内容逐章拼接 ----------
ChapterSortKey extract_chapter_sort_key(const std::string& name) {
    // 支持 "1. 标题"、"1.5 标题"、"2, 标题"、"3 标题"、"4/标题" 等
    static const std::regex re(R"(^\s*([0-9]+(?:\.[0-9]+)?)(?:[\s\.,，、/／:：;；\-_]+|$))");
    std::smatch match;
    ChapterSortKey key;
    if (std::regex_search(name, match, re) && match.size() > 1) {
        try {
            key.has_number = true;
            key.number = std::stod(match[1].str());
        } catch (...) {
            key.has_number = false;
            key.number = 0.0;
        }
    }
    return key;
}

std::vector<std::string> sort_chapters_for_export(std::vector<std::string> chapters, const std::string& order) {
    (void)order;
    bool desc = false;
    std::sort(chapters.begin(), chapters.end(), [&](const std::string& a, const std::string& b) {
        ChapterSortKey ka = extract_chapter_sort_key(a);
        ChapterSortKey kb = extract_chapter_sort_key(b);
        if (ka.has_number && kb.has_number) {
            if (ka.number != kb.number) {
                return desc ? ka.number > kb.number : ka.number < kb.number;
            }
            return a < b;
        }
        if (ka.has_number != kb.has_number) {
            return ka.has_number; // 有序号的永远在前
        }
        return a < b; // 无序号章节始终在末尾，内部按字典序
    });
    return chapters;
}

std::string xml_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 32);
    for (char ch : input) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::vector<std::string> split_lines_preserve_empty(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (char ch : text) {
        if (ch == '\r') continue;
        if (ch == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line += ch;
        }
    }
    lines.push_back(line);
    return lines;
}

std::string make_docx_paragraph(const std::string& text, bool chapter_title) {
    std::string p;
    if (chapter_title) {
        p += "<w:p><w:pPr><w:spacing w:before=\"240\" w:after=\"160\"/></w:pPr>";
        p += "<w:r><w:rPr><w:b/><w:sz w:val=\"32\"/><w:szCs w:val=\"32\"/></w:rPr>";
        p += "<w:t xml:space=\"preserve\">" + xml_escape(text) + "</w:t></w:r></w:p>";
    } else if (text.empty()) {
        p += "<w:p/>";
    } else {
        p += "<w:p><w:r><w:t xml:space=\"preserve\">" + xml_escape(text) + "</w:t></w:r></w:p>";
    }
    return p;
}

std::string make_docx_novel_title(const std::string& text) {
    if (text.empty()) return "";
    std::string p;
    p += R"(<w:p><w:pPr><w:jc w:val="center"/><w:spacing w:before="0" w:after="480"/></w:pPr>)";
    p += R"(<w:r><w:rPr><w:b/><w:sz w:val="44"/><w:szCs w:val="44"/></w:rPr>)";
    p += R"(<w:t xml:space="preserve">)" + xml_escape(text) + "</w:t></w:r></w:p>";
    p += "<w:p/>";
    return p;
}

std::string build_document_xml(const std::vector<std::string>& chapters, const std::string& type, const std::string& novel_name) {
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)";
    xml += "<w:body>";
    xml += make_docx_novel_title(novel_name);
    for (const auto& chapter : chapters) {
        xml += make_docx_paragraph(chapter, true);
        std::string content = read_latest_content(chapter, type);
        auto lines = split_lines_preserve_empty(content);
        for (const auto& line : lines) {
            xml += make_docx_paragraph(line, false);
        }
        xml += make_docx_paragraph("", false);
    }
    xml += R"(<w:sectPr><w:pgSz w:w="11906" w:h="16838"/>)";
    xml += R"(<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/>)";
    xml += "</w:sectPr></w:body></w:document>";
    return xml;
}

uint32_t crc32_calc(const std::string& data) {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char ch : data) {
        crc = table[(crc ^ ch) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void append_u16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void append_u32(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

std::string build_zip_store(const std::vector<std::pair<std::string, std::string>>& files) {
    struct CentralEntry {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };
    std::string out;
    std::vector<CentralEntry> central;

    for (const auto& file : files) {
        const std::string& name = file.first;
        const std::string& data = file.second;
        uint32_t crc = crc32_calc(data);
        uint32_t offset = static_cast<uint32_t>(out.size());
        uint32_t size = static_cast<uint32_t>(data.size());

        append_u32(out, 0x04034b50u);
        append_u16(out, 20); // version needed
        append_u16(out, 0);  // flags
        append_u16(out, 0);  // store, no compression
        append_u16(out, 0);  // mod time
        append_u16(out, 0);  // mod date
        append_u32(out, crc);
        append_u32(out, size);
        append_u32(out, size);
        append_u16(out, static_cast<uint16_t>(name.size()));
        append_u16(out, 0); // extra len
        out += name;
        out += data;
        central.push_back({name, crc, size, offset});
    }

    uint32_t central_offset = static_cast<uint32_t>(out.size());
    for (const auto& entry : central) {
        append_u32(out, 0x02014b50u);
        append_u16(out, 20); // version made by
        append_u16(out, 20); // version needed
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u32(out, entry.crc);
        append_u32(out, entry.size);
        append_u32(out, entry.size);
        append_u16(out, static_cast<uint16_t>(entry.name.size()));
        append_u16(out, 0); // extra
        append_u16(out, 0); // comment
        append_u16(out, 0); // disk
        append_u16(out, 0); // internal attr
        append_u32(out, 0); // external attr
        append_u32(out, entry.offset);
        out += entry.name;
    }
    uint32_t central_size = static_cast<uint32_t>(out.size()) - central_offset;

    append_u32(out, 0x06054b50u);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, static_cast<uint16_t>(central.size()));
    append_u16(out, static_cast<uint16_t>(central.size()));
    append_u32(out, central_size);
    append_u32(out, central_offset);
    append_u16(out, 0);
    return out;
}

std::string build_export_docx(const std::vector<std::string>& chapters, const std::string& type, const std::string& novel_name) {
    std::string content_types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";

    std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";

    std::vector<std::pair<std::string, std::string>> files = {
        {"[Content_Types].xml", content_types},
        {"_rels/.rels", rels},
        {"word/document.xml", build_document_xml(chapters, type, novel_name)}
    };
    return build_zip_store(files);
}


std::string current_export_timestamp_string() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto ms_total = duration_cast<milliseconds>(now.time_since_epoch()).count();
    const auto ms_part = static_cast<int>(ms_total % 1000);
    const std::time_t now_time_t = system_clock::to_time_t(now);

    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now_time_t);
#else
    localtime_r(&now_time_t, &local_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y%m%d_%H%M%S")
        << "_" << std::setw(3) << std::setfill('0') << ms_part;
    return oss.str();
}

std::string sanitize_ascii_filename(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "novel";
    }
    return out;
}

std::string percent_encode_utf8(const std::string& value) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string make_export_filename_base() {
    const std::string base_name = FLAGS_novel_name.empty() ? "novel" : FLAGS_novel_name;
    return base_name + "_" + current_export_timestamp_string() + ".docx";
}
