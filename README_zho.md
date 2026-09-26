<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  🇨🇳 <b>简体中文</b> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### 电气节点：读取住宅交流和直流电网的电压、电流、功率和电能，并保存其开关规则（已在电脑上测试的核心；固件尚未编写）

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-135%2C789-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-ff9800.svg" alt="Maturity">
</p>

---

**诚实性检查 - 今天真正能运行的部分:** **成熟度：scaffolding。** 核心（PZEM-004T v3 和 PZEM-017 电表的帧、轮询它们的总线、节点的消息和开关规则：电表 53 项检查和开关规则 135,736 项检查）在电脑上用模拟的电表和接触器测试，其生成的消息被 ARMOR-COMMON 接受。**目前还没有固件，没有连接任何电表或接触器，这里也不会开关任何东西。**

---

## 🎯 概述

* **读取电表：** Peacefair **PZEM-004T v3**（交流）和 **PZEM-017**（直流）的 Modbus RTU 帧：CRC、读寄存器的请求以及应答的解码（电压、电流、功率、电能、频率、功率因数、报警）。只构造读请求：向电表写入的请求（地址、阈值、电能清零）在任何地方都不构造。不匹配的应答，或包含任何真实电表都给不出的数值的应答，会被拒绝。
* **总线：** 带多个电表的线路逐个轮询并带超时；保留每个电表最后一次正常读数，静默的电表在十秒后不再发布。
* **消息** `armor/electrical/<节点>/state`：每个通道（一条回路、一条线路、电网入口、一条直流母线）一项，含交流或直流、电压、电流、功率、电能、频率、功率因数、节点所见的开关状态和报警；它属于共享契约，只带状态，绝不带命令。
* **开关规则，** 与任何硬件无关：两个电源之间转换的控制器，绝不同时指令两者，要求开关操作被允许、节点在刚才已布防、两个接触器在整个死区时间内都被确认断开，并把没有显示所指令状态的接触器当作故障，直到被确认才消除。**开关默认关闭，没有任何硬件被驱动。**
* **在哪里显示：** ARMOR-STUDIO 的*电气设计器*绘制住宅的电网，并在每个元件上显示其通道的测量值；ARMOR-SERVER 保存读数、历史和合计。
* **尚未完成：** 节点固件（Wi-Fi、面板、MQTT、串口）、硬件、对真实设施的任何测量以及任何开关操作。

## 📂 仓库结构

```text
ARMOR-ELECTRICAL/
├── core/    pzem.hpp (frames of the PZEM meters), pzem_bus.hpp (the line), electrical_json.hpp (the message), interlock.hpp (the rules for switching), json.hpp
├── tests/   test_meters.cpp, test_interlock.cpp, emit_samples.cpp + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ 开发环境

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # the meters (53 checks) and the switching rules (135,736)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
```

参见[设计](docs/DESIGN.md)、[安全说明](docs/SAFETY.md)、[开关规则](docs/SWITCHING.md)、[协议](docs/PROTOCOLS.md)和[消息](docs/ELECTRICAL_MESSAGES.md)。

## 🔗 相关项目

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）是由若干独立仓库组成的周界安防系统。每个仓库都有自己的版本、测试和 README；家族成员如下：

* **[ARMOR-COMMON](../ARMOR-COMMON)** - 消息契约、验证器、一致性向量和生成的类型
* **[ARMOR-RADAR](../ARMOR-RADAR)** - 适用于 ESP32-S3 的现场节点固件，带三个雷达和自带网页面板
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - 太阳能逆变器与电池的协议，以及网关节点的消息
* **ARMOR-ELECTRICAL** (本仓库) - 电气节点：电表、电网读数消息和开关规则
* **[ARMOR-SERVER](../ARMOR-SERVER)** - 中央协调器：遥测、报警、设备、太阳能读数和摄像头
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - 网页控制台：摄像头、雷达、报警、太阳能和 2D/3D 场地设计器
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - 带实时 2D/3D 雷达的 Android 操作员客户端
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - 会解释决策且从不执行动作的视觉推理策略
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - 带无法伪造确认的离线语音意图
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - 外壳、电子器件和台架验收矩阵
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - 部署、CM5 测试台、备份与 TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - 带可重复故障的离线遥测模拟器
* **[ARMOR-DOCS](../ARMOR-DOCS)** - 架构、安全基线和能力矩阵

## 📚 文档与社区

更多阅读：

* [能力矩阵：哪些已被证实，哪些没有](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [项目目录：版本以及各仓库之间的依赖](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [本仓库的变更记录](CHANGELOG.md)
* [许可证（GPL-3.0-or-later）](LICENSE)
* 问题、想法与反馈：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 许可证

GPL-3.0-or-later - 见 [LICENSE](LICENSE)。
