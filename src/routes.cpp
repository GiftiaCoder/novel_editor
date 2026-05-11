#include "routes.h"
#include "auth.h"
#include "config.h"
#include "docx_export.h"
#include "storage.h"
#include "utils.h"

void register_routes(httplib::Server& svr, const std::vector<CommandHook>& command_hooks) {
    // 登录页不需要认证；首页和编辑页需要 timestamp + fingerprint。
    svr.Get("/login.html", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        const std::string content = read_static_file("login.html");
        if (content.empty()) {
            res.status = 404;
            res.set_content("login.html not found", "text/plain; charset=utf-8");
            return;
        }
        res.set_header("Cache-Control", "no-store");
        res.set_content(content, "text/html; charset=utf-8");
    });

    svr.Get("/index.html", [](const httplib::Request& req, httplib::Response& res) {
        serve_protected_static_html(req, res, "index.html");
    });

    svr.Get("/edit.html", [](const httplib::Request& req, httplib::Response& res) {
        serve_protected_static_html(req, res, "edit.html");
    });

    // 兜底保护所有 HTML 页面：除 /login.html 外，任何 .html 都必须带有效认证。
    // 这样即使 ./www 下以后新增页面，也不会因为 set_base_dir 暴露为匿名可访问。
    svr.Get(R"(/(.+\.html))", [](const httplib::Request& req, httplib::Response& res) {
        std::string filename = req.matches[1];
        if (filename == "login.html") {
            const std::string content = read_static_file("login.html");
            if (content.empty()) {
                res.status = 404;
                res.set_content("login.html not found", "text/plain; charset=utf-8");
                return;
            }
            res.set_header("Cache-Control", "no-store");
            res.set_content(content, "text/html; charset=utf-8");
            return;
        }
        if (!is_safe_static_html_path(filename)) {
            res.status = 400;
            res.set_content("Bad request", "text/plain; charset=utf-8");
            return;
        }
        serve_protected_static_html(req, res, filename);
    });

    svr.Get("/ui-config.json", [](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        const std::string content = read_static_file("ui-config.json");
        if (content.empty()) {
            json err; err["success"] = false; err["error"] = "ui-config.json not found";
            set_encrypted_json(res, err, 404);
            return;
        }
        json payload = json::parse(content);
        set_encrypted_json(res, payload);
    });

    // API: 获取章节列表（返回JSON）
    svr.Get("/api/chapters", [](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::cout << "📖 GET /api/chapters called" << std::endl;
    
        try {
            auto chapters = get_chapter_list();
            std::cout << "Found " << chapters.size() << " chapters" << std::endl;
        
            json json_response;
            json_response["success"] = true;
            json_response["data"] = json::array();
        
            for (const auto& chapter_name : chapters) {
                json chapter_json;
                chapter_json["name"] = chapter_name;
            
                // 检查是否存在章纲和正文
                fs::path outline_path = fs::path(FLAGS_content_root) / "latest" / chapter_name / "outline";
                fs::path body_path = fs::path(FLAGS_content_root) / "latest" / chapter_name / "body";
            
                chapter_json["outline_exists"] = fs::exists(outline_path);
                chapter_json["body_exists"] = fs::exists(body_path);
            
                // 读取预览内容（安全版本）
                if (fs::exists(outline_path)) {
                    std::string content = safe_read_file(outline_path);
                    chapter_json["outline_preview"] = get_safe_preview(content);
                } else {
                    chapter_json["outline_preview"] = "";
                }
            
                if (fs::exists(body_path)) {
                    std::string content = safe_read_file(body_path);
                    chapter_json["body_preview"] = get_safe_preview(content);
                } else {
                    chapter_json["body_preview"] = "";
                }
            
                json_response["data"].push_back(chapter_json);
            }
        
            std::string response_str = json_response.dump();
            set_encrypted_json(res, json_response);
        
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            json error_response;
            error_response["success"] = false;
            error_response["error"] = e.what();
            res.status = 500;
            set_encrypted_json(res, error_response);
        }
    });

    // 创建章节（新方式：POST /api/chapters，章节名在body中）
    svr.Post("/api/chapters", [command_hooks](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        try {
            auto body = parse_encrypted_request_json(req);
            std::string name = body.value("name", "");
            if (name.empty()) {
                json err; err["success"] = false; err["error"] = "章节名不能为空";
                res.status = 400;
                set_encrypted_json(res, err, res.status ? res.status : 400);
                return;
            }
            // 可选：对name进行合法性检查
            if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
                json err; err["success"] = false; err["error"] = "章节名不能包含路径分隔符";
                res.status = 400;
                set_encrypted_json(res, err, res.status ? res.status : 400);
                return;
            }
            if (create_chapter(name)) {
                trigger_command_hooks_async(command_hooks, "create_chapter", name);
                json ok; ok["success"] = true; ok["message"] = "章节创建成功";
                set_encrypted_json(res, ok);
            } else {
                json err; err["success"] = false; err["error"] = "创建失败，可能章节已存在";
                res.status = 400;
                set_encrypted_json(res, err, res.status ? res.status : 400);
            }
        } catch (const std::exception& e) {
            json err; err["success"] = false; err["error"] = std::string("请求解析错误: ") + e.what();
            res.status = 400;
            set_encrypted_json(res, err, res.status ? res.status : 400);
        }
    });

    // API: 获取章节内容
    svr.Get(R"(/api/content/([^/]+)/(outline|body))", 
        [](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string chapter = req.matches[1];
        std::string type = req.matches[2];
    
        std::cout << "GET /api/content/" << chapter << "/" << type << std::endl;
    
        try {
            std::string content = read_latest_content(chapter, type);
        
            json json_response;
            json_response["success"] = true;
            json_response["content"] = content;
            json_response["chapter"] = chapter;
            json_response["type"] = type;
        
            set_encrypted_json(res, json_response);
        } catch (const std::exception& e) {
            json error_response;
            error_response["success"] = false;
            error_response["error"] = e.what();
            res.status = 500;
            set_encrypted_json(res, error_response);
        }
    });

    // API: 保存章节内容
    svr.Post(R"(/api/content/([^/]+)/(outline|body))", 
        [command_hooks](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string chapter = req.matches[1];
        std::string type = req.matches[2];
    
        std::cout << "POST /api/content/" << chapter << "/" << type << std::endl;
    
        try {
            auto body = parse_encrypted_request_json(req);
            std::string content = body.value("content", "");
        
            std::string version_path = save_version_file(chapter, type, content);
            (void)version_path;
            trigger_command_hooks_async(command_hooks, type == "outline" ? "save_outline" : "save_body", chapter);
        
            json json_response;
            json_response["success"] = true;
            json_response["message"] = "保存成功";
        
            set_encrypted_json(res, json_response);
        } catch (const std::exception& e) {
            std::cerr << "Save error: " << e.what() << std::endl;
            json json_response;
            json_response["success"] = false;
            json_response["error"] = e.what();
            res.status = 500;
            set_encrypted_json(res, json_response);
        }
    });

    // API: 重命名章节（兼容前端调用）
    svr.Post(R"(/api/chapters/(.+))", 
        [command_hooks](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string old_name = req.matches[1];
    
        try {
            // 解析JSON请求体
            auto body = parse_encrypted_request_json(req);
            std::string new_name = body.value("new_name", "");
        
            if (new_name.empty()) {
                json error;
                error["success"] = false;
                error["error"] = "新章节名不能为空";
                res.status = 400;
                set_encrypted_json(res, error, res.status ? res.status : 400);
                return;
            }
        
            if (rename_chapter(old_name, new_name)) {
                trigger_command_hooks_async(command_hooks, "rename_chapter", new_name, old_name);
                json success;
                success["success"] = true;
                success["message"] = "重命名成功";
                set_encrypted_json(res, success);
            } else {
                json error;
                error["success"] = false;
                error["error"] = "重命名失败，可能新名称已存在或不合法";
                res.status = 400;
                set_encrypted_json(res, error, res.status ? res.status : 400);
            }
        } catch (const json::parse_error& e) {
            json error;
            error["success"] = false;
            error["error"] = "无效的JSON格式";
            res.status = 400;
            set_encrypted_json(res, error, res.status ? res.status : 400);
        } catch (const std::exception& e) {
            json error;
            error["success"] = false;
            error["error"] = e.what();
            res.status = 500;
            set_encrypted_json(res, error, res.status ? res.status : 400);
        }
    });

    // API: 删除章节
    svr.Delete(R"(/api/chapters/(.+))", 
        [command_hooks](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string chapter = req.matches[1];
    
        std::cout << "DELETE /api/chapters/" << chapter << std::endl;
    
        if (delete_chapter(chapter)) {
            trigger_command_hooks_async(command_hooks, "delete_chapter", chapter);
            json json_response;
            json_response["success"] = true;
            json_response["message"] = "章节删除成功";
            set_encrypted_json(res, json_response);
        } else {
            json json_response;
            json_response["success"] = false;
            json_response["error"] = "删除章节失败";
            res.status = 500;
            set_encrypted_json(res, json_response);
        }
    });

    // API: 重命名章节
    svr.Post(R"(/api/chapters/rename/(.+))", 
        [command_hooks](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string old_name = req.matches[1];
    
        try {
            auto body = parse_encrypted_request_json(req);
            std::string new_name = body.value("new_name", "");
        
            if (rename_chapter(old_name, new_name)) {
                trigger_command_hooks_async(command_hooks, "rename_chapter", new_name, old_name);
                json json_response;
                json_response["success"] = true;
                json_response["message"] = "重命名成功";
                set_encrypted_json(res, json_response);
            } else {
                json json_response;
                json_response["success"] = false;
                json_response["error"] = "重命名失败";
                res.status = 400;
                set_encrypted_json(res, json_response);
            }
        } catch (const std::exception& e) {
            json json_response;
            json_response["success"] = false;
            json_response["error"] = e.what();
            res.status = 500;
            set_encrypted_json(res, json_response);
        }
    });


    // API: 获取已完成 hook 任务的输出消息。前端按 since 轮询；消息保留最近 100 条。
    // 即使没有新消息，也明确返回 JSON，方便前端/排查确认接口工作正常。
    svr.Get("/api/hook-messages", [](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        uint64_t since = 0;
        if (req.has_param("since")) {
            try {
                since = static_cast<uint64_t>(std::stoull(req.get_param_value("since")));
            } catch (...) {
                since = 0;
            }
        }

        json response;
        response["success"] = true;
        response["messages"] = json::array();
        response["count"] = 0;
        response["latest_id"] = g_next_hook_message_id.load() > 0 ? (g_next_hook_message_id.load() - 1) : 0;

        {
            std::lock_guard<std::mutex> lock(g_hook_messages_mutex);

            // 如果服务重启过，前端 localStorage 里的 since 可能大于当前内存消息 id。
            // 这种情况下直接返回空消息和当前 latest_id，让前端知道不是接口无响应。
            if (!g_hook_messages.empty()) {
                response["oldest_id"] = g_hook_messages.front().id;
                response["latest_id"] = g_hook_messages.back().id;
            } else {
                response["oldest_id"] = 0;
            }

            for (const auto& item : g_hook_messages) {
                if (item.id <= since) continue;
                json msg;
                msg["id"] = item.id;
                msg["operation"] = item.operation;
                msg["chapter"] = item.chapter;
                msg["old_chapter"] = item.old_chapter;
                msg["exit_code"] = item.exit_code;
                msg["success"] = item.success;
                msg["message"] = item.message;
                response["messages"].push_back(msg);
            }

            response["count"] = response["messages"].size();
        }

        res.status = 200;
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        res.set_header("Pragma", "no-cache");
        set_encrypted_json(res, response);
    });


    // API: 导出全文 DOCX（章纲/正文）
    svr.Get(R"(/api/export/(outline|body)\.docx)",
        [](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string type = req.matches[1];
        std::string order = "asc";

        try {
            auto chapters = sort_chapters_for_export(get_chapter_list(), order);
            std::string docx = build_export_docx(chapters, type, FLAGS_novel_name);
            std::string filename = make_export_filename_base();

            json payload;
            payload["success"] = true;
            payload["filename"] = filename;
            payload["content_type"] = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
            payload["data_base64"] = base64_encode(docx);
            set_encrypted_json(res, payload);
        } catch (const std::exception& e) {
            json error_response;
            error_response["success"] = false;
            error_response["error"] = e.what();
            res.status = 500;
            set_encrypted_json(res, error_response);
        }
    });

    // 首页路由：无认证或认证错误进入登录；有认证则直接返回首页内容，避免 302 丢失 URL 中的 timestamp/fingerprint。
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        serve_protected_static_html(req, res, "index.html");
    });
}
