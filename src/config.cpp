#include "config.h"

// 命令行参数定义
DEFINE_string(content_root, "./data", "Content root directory for storing novels");
DEFINE_int32(port, 8080, "Server port");
DEFINE_string(host, "0.0.0.0", "Server host address");
DEFINE_string(novel_name, "", "Novel name shown at the beginning of exported DOCX files");
DEFINE_string(hook_config, "", "JSON config file for request-triggered command hooks");
DEFINE_string(auth_username, "admin", "Username for web login/authentication");
DEFINE_string(auth_password, "admin", "Password for web login/authentication");
DEFINE_int32(auth_timestamp_tolerance_ms, 300000, "Allowed timestamp skew for auth requests in milliseconds");
