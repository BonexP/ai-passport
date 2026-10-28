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

## 8. 在 Windows 上开发

在 Windows 上构建和烧录是官方支持的，也不难。摩擦全在仓库自己的 shell 门禁，不在固件。

### 8.1 按文档就能用的部分

- ESP-IDF Tools Installer 装 v5.5.3 工具链后，`idf.py build`、`idf.py flash`、`idf.py monitor`
  与 Linux 上表现一致。用安装器生成的 ESP-IDF Command Prompt 执行即可。
- Git 操作与 Python 测试套件是跨平台的。Python 测试用 `pathlib`，没有 POSIX 专用路径。
- 主机测试本身是跨平台的 C：`tests/*.c` 只 include `assert.h`、`stddef.h`、`stdio.h`、
  `stdlib.h`、`string.h`，没有 POSIX 专有依赖。

### 8.2 门禁脚本假设了什么

`tools/validate.sh` 假设有 POSIX shell 以及：

- 支持 `set -euo pipefail` 的 bash
- `mktemp -d /tmp/...` 与 `rm -rf`
- 名为 `cc` 或通过 `$CC` 指定的主机 C 编译器
- firmware 模式下的 `install -m 0644`
- `actionlint`，在 `PATH` 上或通过 `ACTIONLINT_BIN` 指定

在 Windows 上，这些落成四个具体缺口：

- **没有主机编译器。** Git Bash 和 ESP-IDF 都不提供 x86 Windows 编译器；RISC-V 交叉工具链
  无法生成 Windows 测试二进制。用 MinGW-w64（MSYS2 的 `mingw-w64-x86_64-gcc` 或 winlibs）
  或 LLVM clang，并把 `CC` 指向它。
- **actionlint 安装脚本没有 Windows 分支。** `tools/install-actionlint.sh` 只处理 Linux 与
  Darwin；Git Bash 里 `uname -s` 报 `MINGW64_NT-*` 或 `MSYS_NT-*`，脚本直接以
  `Unsupported actionlint platform` 退出。改为单独安装 actionlint（winget、scoop 或官方
  `windows_amd64` 归档），放到 `PATH` 上或设 `ACTIONLINT_BIN`。`validate.sh` 在调安装脚本
  之前会先检查这两处，所以这个绕法是受支持的。
- **Git Bash 不保证有 `install -m 0644`。** 它属于 GNU coreutils，firmware 模式的复制步骤
  可能报 `install: command not found`。依赖 `--firmware` 前先确认，或改用 MSYS2 跑门禁。
- **符号链接技能需要提权。** `tools/install_passport_skills.py` 创建目录软链接，Windows 上
  需要开发者模式或管理员权限。失败时脚本会打印 `skills/<name>/SKILL.md` 的原始路径并以非零
  退出。直接读这些文件就满足「五个技能可用」的要求，软链接只是方便。

### 8.3 换行符陷阱

仓库没有 `.gitattributes`。Git for Windows 默认 `core.autocrlf=true`，会把 CRLF 写进工作树，
让首行为 `#!/usr/bin/env bash` 的脚本全部失效，报经典的
`/usr/bin/env: 'bash\r': No such file or directory`。

Windows 主机首次 checkout 之前：

```bash
git config core.autocrlf input
```

按仓库设置即可，不需要改全局。

### 8.4 建议

Windows 上编辑，CI 上验证，Windows 上烧录。这是主路径，而且它让 WSL2 变得完全没必要。

- **编辑**：Windows，只需要 Git 和编辑器，不需要门禁工具链的任何组件。
- **构建与门禁**：CI 的 `ubuntu-latest`，由 pull request 触发。这台装有本地工具链的 Linux 主机
  是快速自检和后备手段，不是必需品。
- **烧录与真机验收**：Windows 原生，在 ESP-IDF Command Prompt 里做。
- **同步**：远端仓库，整个分工靠它成立。

结论：8.2 里的四个缺口一个都不用补。不需要 MSYS2 或 winlibs 编译器，不需要手动装 actionlint，
不需要可移植的 `install`。唯一的硬前提是 8.3：第一次 checkout 前设 `core.autocrlf input`，
并依赖仓库里的 `.gitattributes` 把脚本固定为 LF。

有一条必须记住：CI 只是编译，它看不见设备。PR 变绿只证明 Build 与 Host tests；屏幕、按键、
音频、电池、Wi-Fi 行为仍然属于真机结果，在有人拿着板子确认之前一律记 `Unverified`。

如果将来确实要求 Windows 原生门禁，工作量是可控的：检查 `cc` 是否存在、检查 `actionlint`
路径、用可移植方式替换 `install -m 0644`。这些仍作为条件任务列在 `task-breakdown.md` 里。
