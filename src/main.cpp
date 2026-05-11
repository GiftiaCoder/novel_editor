#include "common.h"
#include "command_hooks.h"
#include "config.h"
#include "routes.h"
#include "utils.h"

int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 确保数据目录存在
    ensure_directory(FLAGS_content_root);
    ensure_directory(fs::path(FLAGS_content_root) / "versions");
    ensure_directory(fs::path(FLAGS_content_root) / "latest");

    httplib::Server svr;

    // 设置静态文件目录
    svr.set_base_dir("./www");

    // 未配置 --hook_config 时 hooks 为空，不执行任何 hook 任务。
    // hook 只在五类业务写操作成功后触发，并在独立线程中执行，不影响 server 响应。
    const auto command_hooks = load_command_hooks(FLAGS_hook_config);

    register_routes(svr, command_hooks);

    std::cout << "========================================" << std::endl;
    std::cout << "📚 小说编辑器服务器已启动" << std::endl;
    std::cout << "🌐 访问地址: http://" << FLAGS_host << ":" << FLAGS_port << std::endl;
    std::cout << "📁 数据目录: " << FLAGS_content_root << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "测试命令: curl http://" << FLAGS_host << ":" << FLAGS_port << "/api/chapters" << std::endl;

    svr.listen(FLAGS_host, FLAGS_port);

    return 0;
}
