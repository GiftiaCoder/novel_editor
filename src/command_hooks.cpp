#include "command_hooks.h"
#include "config.h"
#include "utils.h"

#include <sys/wait.h>

// ---------- 请求触发命令 Hook ----------
// Hook 配置示例：
// {
//   "hooks": [
//     { "operation": "create_chapter", "command": "echo hook" },
//     { "operation": "rename_chapter", "command": "echo hook" },
//     { "operation": "delete_chapter", "command": "echo hook" },
//     { "operation": "save_outline", "command": "echo hook" },
//     { "operation": "save_body", "command": "echo hook" }
//   ]
// }
// 兼容旧配置：如果没有 operation，也可以继续写 method/path/command；服务端会把它们映射到上述五类操作。
// 命中后会异步执行：command content_root operation chapter old_chapter
// 其中 old_chapter 只在 rename_chapter 时有值，其他操作传空字符串。
std::string to_upper_ascii(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

// 用 POSIX shell 的单引号规则包裹一个参数。
// 例如：abc -> 'abc'
//      a'b -> 'a'\''b'
// 这样传给 hook 命令的每个追加参数都能作为一个完整 argv 被 shell 解析。
std::string shell_quote_arg(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

bool is_supported_hook_operation(const std::string& operation) {
    return operation == "create_chapter" ||
           operation == "rename_chapter" ||
           operation == "delete_chapter" ||
           operation == "save_outline" ||
           operation == "save_body";
}

std::string operation_from_legacy_method_path(const std::string& method_raw, const std::string& path) {
    const std::string method = to_upper_ascii(method_raw);

    if (method == "POST" && path == "/api/chapters") {
        return "create_chapter";
    }
    if (method == "POST" && path == "/api/chapters/(.+)") {
        return "rename_chapter";
    }
    if (method == "POST" && path == "/api/chapters/rename/(.+)") {
        return "rename_chapter";
    }
    if (method == "DELETE" && path == "/api/chapters/(.+)") {
        return "delete_chapter";
    }
    if (method == "POST" && path == "/api/content/([^/]+)/outline") {
        return "save_outline";
    }
    if (method == "POST" && path == "/api/content/([^/]+)/body") {
        return "save_body";
    }
    if (method == "POST" && path == "/api/content/([^/]+)/(outline|body)") {
        return "save_outline,save_body";
    }

    return "";
}

std::vector<CommandHook> load_command_hooks(const std::string& config_path) {
    std::vector<CommandHook> hooks;
    if (config_path.empty()) {
        return hooks;
    }

    try {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            std::cerr << "Hook config not found or cannot open: " << config_path << std::endl;
            return hooks;
        }

        json config;
        file >> config;

        if (!config.contains("hooks") || !config["hooks"].is_array()) {
            std::cerr << "Hook config ignored: root field 'hooks' must be an array" << std::endl;
            return hooks;
        }

        for (const auto& item : config["hooks"]) {
            const std::string command = item.value("command", "");
            if (command.empty()) {
                std::cerr << "Skip invalid hook: command is required" << std::endl;
                continue;
            }

            std::string operation = item.value("operation", "");
            if (operation.empty()) {
                operation = operation_from_legacy_method_path(item.value("method", ""), item.value("path", ""));
            }

            if (operation == "save_outline,save_body") {
                hooks.push_back({"save_outline", command});
                hooks.push_back({"save_body", command});
                continue;
            }

            if (!is_supported_hook_operation(operation)) {
                std::cerr << "Skip unsupported hook operation: " << operation << std::endl;
                continue;
            }

            hooks.push_back({operation, command});
        }

        std::cout << "Loaded " << hooks.size() << " command hook(s) from " << config_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load hook config: " << e.what() << std::endl;
    }

    return hooks;
}

std::mutex g_hook_messages_mutex;
std::deque<HookMessage> g_hook_messages;
std::atomic<uint64_t> g_next_hook_message_id{1};

std::string read_whole_file_binary(const fs::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string trim_for_hook_message(std::string value, size_t max_len) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    if (value.size() > max_len) {
        value = value.substr(0, max_len) + "\n...";
    }
    return value;
}

void push_hook_message(const std::string& operation,
                       const std::string& chapter,
                       const std::string& old_chapter,
                       int exit_code,
                       bool success,
                       const std::string& message) {
    std::string clean_message = trim_for_hook_message(message);
    if (clean_message.empty()) {
        clean_message = success ? "hook 执行成功" : "hook 执行失败";
    }

    HookMessage item;
    item.id = g_next_hook_message_id.fetch_add(1);
    item.operation = operation;
    item.chapter = chapter;
    item.old_chapter = old_chapter;
    item.exit_code = exit_code;
    item.success = success;
    item.message = clean_message;

    std::lock_guard<std::mutex> lock(g_hook_messages_mutex);
    g_hook_messages.push_back(item);
    while (g_hook_messages.size() > 100) {
        g_hook_messages.pop_front();
    }
}

int decode_system_exit_code(int status) {
    if (status == -1) return -1;
#ifdef WIFEXITED
    if (WIFEXITED(status)) return WEXITSTATUS(status);
#endif
#ifdef WIFSIGNALED
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
#endif
    return status;
}

void run_linux_command_and_record_message(const std::string& command,
                                          const std::string& operation,
                                          const std::string& chapter,
                                          const std::string& old_chapter) {
    fs::path tmp_dir = fs::temp_directory_path();
    std::string token = "novel_hook_" + generate_uuid();
    fs::path stdout_path = tmp_dir / (token + ".out");
    fs::path stderr_path = tmp_dir / (token + ".err");

    std::string full_command = command;
    full_command += " > " + shell_quote_arg(stdout_path.string());
    full_command += " 2> " + shell_quote_arg(stderr_path.string());

    int status = std::system(full_command.c_str());
    int exit_code = decode_system_exit_code(status);
    bool success = (exit_code == 0);

    std::string stdout_content = read_whole_file_binary(stdout_path);
    std::string stderr_content = read_whole_file_binary(stderr_path);

    std::error_code ec;
    fs::remove(stdout_path, ec);
    fs::remove(stderr_path, ec);

    const std::string& selected = success ? stdout_content : stderr_content;
    push_hook_message(operation, chapter, old_chapter, exit_code, success, selected);

    if (!success) {
        std::cerr << "Hook command exited with code " << exit_code << ": " << command << std::endl;
    }
}

void trigger_command_hooks_async(const std::vector<CommandHook>& hooks,
                                 const std::string& operation,
                                 const std::string& chapter,
                                 const std::string& old_chapter) {
    if (hooks.empty() || !is_supported_hook_operation(operation)) {
        return;
    }

    for (const auto& hook : hooks) {
        if (hook.operation == operation) {
            std::string command = hook.command;
            command += " " + shell_quote_arg(FLAGS_content_root);
            command += " " + shell_quote_arg(operation);
            command += " " + shell_quote_arg(chapter);
            command += " " + shell_quote_arg(old_chapter);

            std::thread([command, operation, chapter, old_chapter]() {
                run_linux_command_and_record_message(command, operation, chapter, old_chapter);
            }).detach();
        }
    }
}
