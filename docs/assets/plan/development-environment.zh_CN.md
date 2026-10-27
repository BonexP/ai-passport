<p align="right">
  <strong>简体中文</strong> · <a href="development-environment.md">English</a>
</p>

# 开发环境

状态：2026-10-27 已安装并验证。本文件记录装了些什么、怎么激活、以及保证可复现的边界。
它不取代上游的环境引导文档，只记录本机的实际结果。

## 1. 装了什么

| 组件 | 版本 | 位置 | 备注 |
| --- | --- | --- | --- |
| ESP-IDF | v5.5.3 | `~/esp/esp-idf-v5.5.3` | Gitee 镜像克隆；tag 提交 `b31fcc7a314a44ad992b58f589f7d1d8a4fadff6` 与上游一致 |
| 工具链 | riscv32-esp-elf 14.2.0、openocd-esp32、esp-rom-elfs | `~/.espressif/tools` | 2.2 GB |
| IDF Python 环境 | `idf5.5_py3.12_env`（Python 3.12.3） | `~/.espressif/python_env` | 由 `install.sh` 创建，含 `idf-component-manager` |
| ccache | 4.9.1 | 系统 | 主机包 |
| gperf | 3.1 | 系统 | 主机包，IDF 构建必需 |
| uv | 0.11.7 | `~/.local/bin/uv` | 仅服务仓库侧工具，见第 4 节 |

验证方式：`. ~/esp/esp-idf-v5.5.3/export.sh && idf.py --version`，输出 `ESP-IDF v5.5.3-dirty`。

## 2. 版本号为什么带 "dirty"

`idf.py --version` 报 `v5.5.3-dirty`。源码树本身没有改动（`git status --porcelain`
在顶层为空）。后缀来自 22 个子模块指针与锁定值不一致——Gitee 镜像把每个子模块从它自己的
镜像仓库提供，head 提交不同。

后果：

- 不影响构建；差异在子模块记账，不在源码内容。
- `git describe` 会追加 `-dirty`，任何解析版本号的工具都要接受这个后缀，不能只认 `v5.5.3`。
- 不要为了"好看"把子模块重置到锁定提交，除非某个具体故障要求这么做；镜像 head 就是镜像
  打算交付的东西。
- 如果确实需要干净的版本字符串，在网络允许时从 GitHub 重新克隆并重跑 `install.sh`。
  当作独立的、有理由的任务处理。

## 3. 激活

环境不注入 shell profile，按会话激活：

```bash
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py --version    # 期望输出：ESP-IDF v5.5.3-dirty
```

刻意没有把 `export.sh` 写进 `.bashrc` 或 `.profile`：那会改变每个 shell 的 `PATH`，
而上游指南禁止未经明确授权修改 shell 启动文件。

## 4. uv：定位与边界

`uv` 可用也有用，但它**不替代** ESP-IDF 的 Python 环境。

- ESP-IDF 的 `install.sh` 调用 `python -m venv` 创建
  `~/.espressif/python_env/idf5.5_py3.12_env`，`export.sh` 把该环境放到 `PATH` 最前面，
  `idf_tools.py` 直接管理它。改成 uv 意味着给 IDF 自己的安装器打补丁，并且每次上游更新
  都要重新打。
- 因此：**IDF 保留官方 venv，uv 只服务仓库侧的 Python 工作**——主机测试辅助脚本、格式化与
  静态检查、以及未来属于本 fork 而非 ESP-IDF 的构建自动化。
- 仓库侧一旦出现 Python 依赖，就在仓库根放 `pyproject.toml` 用 `uv run` 执行，不依赖
  环境里已有的东西。
- 不要让 `uv run` 遮蔽 IDF 环境：IDF 命令在 source 过 `export.sh` 的 shell 里跑，仓库工具
  另外跑。

这个分工是决策，不是临时状态。要改，代价是给 `idf_tools.py` 打补丁，这个代价必须被明确论证。

## 5. 校验入口

上游门禁脚本有三档，含义不同：

```bash
./tools/validate.sh --static     # 仓库检查、actionlint、全部主机测试
./tools/validate.sh --firmware   # ESP-IDF 构建 + 合并镜像校验
./tools/validate.sh              # 两者都跑
```

`--firmware` 需要已激活环境。它编译到临时目录，成功后才把
`build/FoloToy-AI-Passport-full.bin` 拷出来，所以不会在工作树里留下长期构建目录。

交付报告按上游规则保持四行分开：Build、Host tests、Device tests、Unverified。
编译通过不等于真机结果。

## 6. 设备访问

构建可用不代表可以烧录和监视。板子走原生 USB Serial/JTAG（GPIO18/19）；UART0 不可用，
因为其默认 TX 引脚与屏幕背光冲突。串口访问、端口权限（`dialout`）和物理连接是三个独立检查，
且设备必须上电并被枚举才能执行任何刷写操作。截至目前没有执行过任何刷写或监视。

## 7. 已知限制

- 本机到 GitHub 的网络不稳定：直连克隆会卡住或 TLS 中断，因此改用 Gitee 镜像；工具归档来自
  `dl.espressif.cn`。
- 该 checkout 是完整克隆而非浅克隆，保留了 IDF 的 Git 历史；IDF 加工具链共约 3.2 GB。
- 安装时可用磁盘约 28 GB。构建产物和 `managed_components` 会持续占用，加入大素材或字体前
  先看剩余空间。
