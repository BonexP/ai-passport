<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# BonexP 的 AI Passport fork

[FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的个人 fork，承载 AI Passport
设备（ESP32-C3、8 MB Flash、无 PSRAM）的集大成自定义固件。

上游文档原样保留，仍然是权威来源：产品概览从
[`docs/README.zh_CN.md`](docs/README.zh_CN.md) 开始，开发规则看
[`AGENTS.zh_CN.md`](AGENTS.zh_CN.md)，fork 工作流看
[`docs/fork-guide.zh_CN.md`](docs/fork-guide.zh_CN.md)。

## 本 fork 增加的内容

| 路径 | 内容 |
| --- | --- |
| [`docs/assets/plan/project-plan.zh_CN.md`](docs/assets/plan/project-plan.zh_CN.md) | 范围、硬件边界、功能评估、分期计划 |
| [`docs/assets/plan/branch-model.zh_CN.md`](docs/assets/plan/branch-model.zh_CN.md) | 远程、分支布局、上游同步流程 |

## 状态

规划中，尚无应用代码。基线为上游 `main`，未修改。

## 分支模型，一段话说完

`main` 镜像 `upstream/main`，只多出本文件和 `docs/assets/`。所有固件工作在
`feature/my-custom` 上进行——这是从 `upstream/main` 拉出的长期集成分支。单个功能与修复
从 `feature/my-custom` 切出，完成后合回。上游改动先取回、快进到 `main`，再合并进
`feature/my-custom`。

## 开始

```bash
git clone https://github.com/BonexP/ai-passport.git
cd ai-passport
python3 tools/install_passport_skills.py --check
```

需要 ESP-IDF v5.5.3，见
[`docs/development/engineering/environment-setup.zh_CN.md`](docs/development/engineering/environment-setup.zh_CN.md)。
每次提交前跑 `./tools/validate.sh --static`，交付前跑完整的 `./tools/validate.sh`。

## 许可

继承上游，见 [`LICENSE`](LICENSE)。
