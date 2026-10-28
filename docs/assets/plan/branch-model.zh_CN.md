<p align="right">
  <strong>简体中文</strong> · <a href="branch-model.md">English</a>
</p>

# 分支与同步模型

本文定义本 fork 与 `FoloToy/ai-passport` 的关系。它在官方
[fork 工作流](../../fork-guide.zh_CN.md) 之上只加一件事：一个长期存活的集成分支。

## 1. 远程

| 远程 | 地址 | 作用 |
| --- | --- | --- |
| `origin` | `https://github.com/BonexP/ai-passport` | 我们的 fork，所有推送走这里。 |
| `upstream` | `https://github.com/FoloToy/ai-passport` | 只读基线来源。 |

开工前两边都取一次：

```bash
git fetch origin
git fetch upstream
```

## 2. 分支布局

| 分支 | 生命周期 | 内容 | 规则 |
| --- | --- | --- | --- |
| `main` | 永久 | `upstream/main` 镜像 + fork 级文件 | 只允许根 `README.md` 与 `docs/assets/` 有差异，绝不承载功能代码。 |
| `feature/my-custom` | 长期 | 集成分支：集大成固件 | 接收上游合并与完成后的主题分支，始终保持可构建。 |
| `feature/<topic>` | 短期 | 单次修复或单个功能 | 从 `feature/my-custom` 拉出，合回后删除。 |

代码改动发生在 `feature/<topic>` 上。例如：`feature/wifi-profiles`、`feature/stock-page`、
`feature/weather-page`、`fix/wifi-reconnect-loop`。

## 3. 创建集成分支

从上游创建，不要从 fork 的 `main` 创建，避免过期的 main 污染起点：

```bash
git fetch upstream
git switch -c feature/my-custom upstream/main
git push -u origin feature/my-custom
```

## 4. 主题分支的工作流

```bash
git fetch origin
git switch feature/my-custom
git pull --ff-only origin feature/my-custom
git switch -c feature/wifi-profiles
# ... 改代码，提交 ...
./tools/validate.sh --static
git push -u origin feature/wifi-profiles
```

合回集成分支时用合并提交（不要压缩成快进），让主题边界留在历史里：

```bash
git switch feature/my-custom
git merge --no-ff feature/wifi-profiles
git push origin feature/my-custom
```

## 5. 吸收上游改动

`main` 由 fork 的 `Upstream Sync` 工作流自动保持最新
（`.github/workflows/sync-main.yml`，守卫条件 `if: github.event.repository.fork`，每天
00:00 UTC）。fork 里必须手动启用 GitHub Actions 才会生效。

无论该工作流是否启用，本地流程都一样：

```bash
git fetch upstream
git switch main
git merge --ff-only upstream/main       # 正常情况永不冲突
git push origin main

git switch feature/my-custom
git merge main                            # 可能冲突，在这里解决
git push origin feature/my-custom
```

原则：

- 把 `main` 合并进 `feature/my-custom`，不要用 rebase。对已推送分支做 rebase 只会重写
  共享历史，在这里没有任何收益。
- 基础设施文件（`tools/`、`.github/`、`components/bsp/`、`docs/assets/` 之外的 `docs/`）
  冲突时以上游为准。我们的应用改动重新叠在上游之上，而不是去改上游的工具。
- 每次合并上游后，先跑 `./tools/validate.sh --static` 再继续。
- 上游改动到我们已用的 `components/bsp` API 时，当作一个正常的重构任务放到主题分支，
  不要顺手在内联修改里解决。

## 6. 不进 fork 的东西

- 凭证、API key、设备秘密、私钥。改为运行时配进 NVS。
- 构建产物（`build/`、`managed_components/`、`sdkconfig`）。
- `docs/assets/` 之外的 fork 专属产品文档。
- `main` 上对上游文件的任何修改，除根 `README.md` 之外。

## 7. 回馈上游

对任何 AI Passport 用户都有益的改进——更正硬件事实、可复用的 BSP 接口、构建流程修复——
从 `upstream/main` 切主题分支，作为 PR 提交上游。应用归档放到
`docs/reference/<用户名>/<应用名>/`。发布后的流程用 `experience-pr` 与 `plays-archive` 技能。

fork 专属的产品决策留在这个 fork，永不回流上游。

## 8. 怎么触发 CI

在以 Windows 为主的流程里，CI 是主要验证路径。它的触发条件是具体的，只推送不够。

| Workflow | 触发条件 | 跑什么 |
| --- | --- | --- |
| `static-checks.yml` | pull request、push 到 `main`、手动 dispatch | 仓库检查、actionlint、全部主机测试 |
| `firmware-checks.yml` | pull request、push 到 `main`、手动 dispatch | 完整 ESP-IDF 构建与合并镜像校验 |
| `build-firmware.yml` | push tag、手动 dispatch | 构建并创建 GitHub Release |

实务上必须记住的几点：

- 推送到 `feature/my-custom` 或任何非 `main` 分支**不会触发任何检查**。只有 pull request 才会。
- `firmware-checks.yml` 忽略只改 `docs/**`、`skills/**`、`*.md` 的变更。纯文档的 PR 仍会跑
  `static-checks.yml`，但不会构建固件。
- 三个 workflow 都有 `workflow_dispatch`，所以可以在 Actions 页面手动跑一次，不必为此开 PR。
  当某个分支推送需要验证、但还不想走评审时，这是逃生口。

推荐流程：

1. 在 `feature/my-custom`（或 `feature/<topic>` 分支）上提交并推送。
2. 开 pull request。只做自检时 base 可以直接是 `feature/my-custom`；要认真评审时，用 topic 分支
   对 `feature/my-custom` 开 PR。
3. 等 `Static checks` 与 `Firmware checks` 都变绿。
4. 用合并提交合入，保留主题边界。

CI 只做编译与主机测试，它看不见设备。屏幕、按键、音频、电池、Wi-Fi 行为在交付报告里
一律记为 `Unverified`，直到有人拿着板子确认。
