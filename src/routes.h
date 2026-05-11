#pragma once

#include "common.h"
#include "command_hooks.h"

void register_routes(httplib::Server& svr, const std::vector<CommandHook>& command_hooks);
