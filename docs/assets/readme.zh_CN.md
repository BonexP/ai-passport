<p align="right">
  <strong>简体中文</strong> · <a href="readme.md">English</a>
</p>

# docs/assets：fork 专属文档

本目录存放属于本 fork、不属于上游的文档。上游 `main` 上这里除 `.gitkeep` 外保持为空；
这里的一切只存在于本 fork，永不回流上游。

## 结构

```
docs/assets/
  readme.md, readme.zh_CN.md      本文件：用途与约定。
  plan/
    project-plan.md               范围、硬件边界、功能评估、分期。
    branch-model.md               远程、分支布局、上游同步流程。
```

## 约定

- 每份文档双语：英文 `.md` 默认版 + 中文 `.zh_CN.md` 配对版，顶部互相链接。
  `tools/check_repo.py` 会强制检查这个配对。
- 英文默认版不得含中文散文。
- 板卡事实遵循上游的事实优先级：实测产品数据 > `components/bsp/include/bsp_pins.h` >
  BSP 头文件 > 硬件指南 > README 与 demo。
- 对任何 AI Passport 用户都有用的内容不属于这里，应提 PR 给上游。

## 边界

本目录的文件允许在 `main` 上修改，与根 `README.md` 同级。这是「`main` 镜像上游」规则
唯一的例外。应用代码以及任何影响构建的东西都在 `feature/*` 分支上。
