#pragma once

#include "common.h"

uint64_t fnv1a64(const std::string& value);
std::string hex_u64(uint64_t value);
std::string make_auth_fingerprint(const std::string& username, const std::string& password, const std::string& timestamp);
int64_t current_epoch_ms();
bool constant_time_equal(const std::string& a, const std::string& b);
std::string base64_encode(const std::string& input);
std::string base64_decode(const std::string& input);
std::string xor_crypt(const std::string& input, const std::string& timestamp);
std::string encrypted_json_response(const json& original_payload);
void set_encrypted_json(httplib::Response& res, const json& original_payload, int status = 0);
void set_plain_auth_error(httplib::Response& res, const std::string& message = "认证失败", int status = 401);
json parse_encrypted_request_json(const httplib::Request& req);
bool require_auth(const httplib::Request& req, httplib::Response& res);
std::string read_static_file(const std::string& relative_path);
void redirect_to_login(httplib::Response& res);
bool is_safe_static_html_path(const std::string& path);
void serve_protected_static_html(const httplib::Request& req, httplib::Response& res, const std::string& filename);
