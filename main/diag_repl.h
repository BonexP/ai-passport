// main/diag_repl.h —— 可选 USB Serial/JTAG 诊断 REPL 的入口与 main.c 绑定。
//
// REPL 默认关闭（CONFIG_FOLOTOY_DIAG_REPL）。页面状态只由 main.c 拥有，因此
// page 命令通过下面的绑定函数走与按键完全相同的 page_switch() 路径。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "page_router.h"

// 启动 REPL 任务。只在 CONFIG_FOLOTOY_DIAG_REPL=y 时由 app_main() 调用。
// 失败只记录日志，不阻塞产品启动。
void diag_repl_start(void);

// 以下函数由 main.c 实现，供 diag_repl.c 读取/切换页面。
page_id_t diag_current_page(void);
const char *diag_page_name(page_id_t page);
const char *diag_boot_state_name(void);
bool diag_services_paused(void);

// 只允许 STATUS / WEATHER / USAGE 三页，且当前页必须也在这三页之内。
// 成功返回 true；失败时把原因写入 err（可为 NULL）。
bool diag_page_switch(page_id_t target, char *err, size_t cap);
