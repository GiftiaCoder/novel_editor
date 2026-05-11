#include "auth.h"
#include "config.h"
#include "utils.h"

// ---------- 轻量认证与响应 payload 加密 ----------
// 协议：客户端请求带 X-Auth-Timestamp 和 X-Auth-Fingerprint。
// fingerprint = fnv1a64_hex(username + "\n" + password + "\n" + timestamp)
// JSON 响应：{success:true, timestamp:"...", payload:"..."}
// payload = base64(xor_stream(original_json_utf8, username + password + timestamp))
// 说明：这是应用层访问控制与混淆，不替代 HTTPS。
uint64_t fnv1a64(const std::string& value) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : value) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex_u64(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    return oss.str();
}

std::string make_auth_fingerprint(const std::string& username, const std::string& password, const std::string& timestamp) {
    return hex_u64(fnv1a64(username + "\n" + password + "\n" + timestamp));
}

int64_t current_epoch_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string url_decode_component(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            long decoded = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return out;
}

std::string get_cookie_value(const httplib::Request& req, const std::string& name) {
    if (!req.has_header("Cookie")) return "";
    const std::string cookie = req.get_header_value("Cookie");
    const std::string key = name + "=";
    size_t start = 0;
    while (start < cookie.size()) {
        size_t end = cookie.find(';', start);
        std::string part = cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        if (part.rfind(key, 0) == 0) {
            return url_decode_component(part.substr(key.size()));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

std::string get_auth_value(const httplib::Request& req, const std::string& header_name, const std::string& param_name) {
    // fetch/XHR 请求优先使用 Header；保留 URL 参数兼容旧链接。
    if (req.has_header(header_name)) return req.get_header_value(header_name);
    if (req.has_param(param_name)) return req.get_param_value(param_name);
    return "";
}

bool validate_auth_cookie(const httplib::Request& req, std::string* error) {
    const std::string username = get_cookie_value(req, "novelAuthUsername");
    const std::string password = get_cookie_value(req, "novelAuthPassword");
    if (username.empty() || password.empty()) {
        if (error) *error = "缺少认证信息";
        return false;
    }
    if (!constant_time_equal(username, FLAGS_auth_username) || !constant_time_equal(password, FLAGS_auth_password)) {
        if (error) *error = "用户名或密码错误";
        return false;
    }
    return true;
}

bool validate_auth_request(const httplib::Request& req, std::string* error) {
    const std::string timestamp = get_auth_value(req, "X-Auth-Timestamp", "timestamp");
    const std::string fingerprint = get_auth_value(req, "X-Auth-Fingerprint", "fingerprint");

    if (timestamp.empty() || fingerprint.empty()) {
        if (error) *error = "缺少认证信息";
        return false;
    }

    int64_t client_ms = 0;
    try {
        client_ms = std::stoll(timestamp);
    } catch (...) {
        if (error) *error = "时间戳格式错误";
        return false;
    }

    const int64_t tolerance_ms = static_cast<int64_t>(FLAGS_auth_timestamp_tolerance_ms);
    if (tolerance_ms >= 0) {
        const int64_t diff = std::llabs(current_epoch_ms() - client_ms);
        if (diff > tolerance_ms) {
            if (error) *error = "认证时间戳已过期";
            return false;
        }
    }

    const std::string expected = make_auth_fingerprint(FLAGS_auth_username, FLAGS_auth_password, timestamp);
    if (!constant_time_equal(expected, fingerprint)) {
        if (error) *error = "用户名或密码错误";
        return false;
    }
    return true;
}

const char* BASE64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string& input) {
    std::string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(BASE64_ALPHABET[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(BASE64_ALPHABET[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string base64_decode(const std::string& input) {
    std::vector<int> table(256, -1);
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(BASE64_ALPHABET[i])] = i;
    }

    std::string out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        if (table[c] == -1) continue;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string xor_crypt(const std::string& input, const std::string& timestamp) {
    uint64_t state = fnv1a64(FLAGS_auth_username + "\n" + FLAGS_auth_password + "\n" + timestamp);
    std::string out = input;
    for (size_t i = 0; i < out.size(); ++i) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        uint64_t rnd = state * 2685821657736338717ull;
        out[i] = static_cast<char>(static_cast<unsigned char>(out[i]) ^ static_cast<unsigned char>((rnd >> ((i % 8) * 8)) & 0xFF));
    }
    return out;
}

std::string encrypted_json_response(const json& original_payload) {
    const std::string timestamp = std::to_string(current_epoch_ms());
    json wrapped;
    wrapped["success"] = true;
    wrapped["timestamp"] = timestamp;
    wrapped["payload"] = base64_encode(xor_crypt(original_payload.dump(), timestamp));
    return wrapped.dump();
}

void set_encrypted_json(httplib::Response& res, const json& original_payload, int status) {
    res.status = status > 0 ? status : (res.status > 0 ? res.status : 200);
    res.set_header("Cache-Control", "no-store");
    res.set_content(encrypted_json_response(original_payload), "application/json; charset=utf-8");
}

void set_plain_auth_error(httplib::Response& res, const std::string& message, int status) {
    json err;
    err["success"] = false;
    err["error"] = message;
    err["login"] = "/login.html";
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_header("X-Auth-Redirect", "/login.html");
    res.set_content(err.dump(), "application/json; charset=utf-8");
}

json parse_encrypted_request_json(const httplib::Request& req) {
    json wrapped = json::parse(req.body);
    const std::string timestamp = wrapped.value("timestamp", "");
    const std::string payload = wrapped.value("payload", "");

    if (timestamp.empty() || payload.empty()) {
        throw std::runtime_error("缺少加密请求 payload");
    }

    const std::string auth_timestamp = get_auth_value(req, "X-Auth-Timestamp", "timestamp");
    if (!auth_timestamp.empty() && !constant_time_equal(auth_timestamp, timestamp)) {
        throw std::runtime_error("请求 timestamp 不一致");
    }

    return json::parse(xor_crypt(base64_decode(payload), timestamp));
}

bool require_auth(const httplib::Request& req, httplib::Response& res) {
    std::string error;
    if (validate_auth_request(req, &error)) return true;
    set_plain_auth_error(res, error);
    return false;
}

std::string read_static_file(const std::string& relative_path) {
    return safe_read_file(fs::path("./www") / relative_path);
}

void redirect_to_login(httplib::Response& res) {
    res.status = 302;
    res.set_header("Cache-Control", "no-store");
    res.set_redirect("/login.html");
}

bool is_safe_static_html_path(const std::string& path) {
    if (path.empty()) return false;
    if (path.find("..") != std::string::npos) return false;
    if (path.front() == '/') return false;
    return path.size() >= 5 && path.rfind(".html") == path.size() - 5;
}

void serve_protected_static_html(const httplib::Request& req, httplib::Response& res, const std::string& filename) {
    std::string error;
    if (!validate_auth_request(req, &error)) {
        redirect_to_login(res);
        return;
    }
    const std::string content = read_static_file(filename);
    if (content.empty()) {
        res.status = 404;
        res.set_content("Not found", "text/plain; charset=utf-8");
        return;
    }
    res.set_header("Cache-Control", "no-store");
    res.set_content(content, "text/html; charset=utf-8");
}
