#pragma once

#include "common.h"

struct CommandHook {
    std::string operation;
    std::string command;
};

struct HookMessage {
    uint64_t id;
    std::string operation;
    std::string chapter;
    std::string old_chapter;
    int exit_code;
    bool success;
    std::string message;
};

extern std::mutex g_hook_messages_mutex;
extern std::deque<HookMessage> g_hook_messages;
extern std::atomic<uint64_t> g_next_hook_message_id;

std::string to_upper_ascii(std::string value);
std::string shell_quote_arg(const std::string& value);
bool is_supported_hook_operation(const std::string& operation);
std::string operation_from_legacy_method_path(const std::string& method_raw, const std::string& path);
std::vector<CommandHook> load_command_hooks(const std::string& config_path);
std::string read_whole_file_binary(const fs::path& file_path);
std::string trim_for_hook_message(std::string value, size_t max_len = 4000);
void push_hook_message(const std::string& operation, const std::string& chapter, const std::string& old_chapter, int exit_code, bool success, const std::string& message);
int decode_system_exit_code(int status);
void run_linux_command_and_record_message(const std::string& command, const std::string& operation, const std::string& chapter, const std::string& old_chapter);
void trigger_command_hooks_async(const std::vector<CommandHook>& hooks, const std::string& operation, const std::string& chapter, const std::string& old_chapter = "");
