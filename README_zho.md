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
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  <a href="README_por.md">🇵🇹 Português</a>
</p>

### 电气节点：读取住宅交流和直流电网的电压、电流、功率和电能，并保存其开关规则（已在电脑上测试的核心和可编译的固件；尚未在电路板上运行过）

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-376%2C195-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Version-0.4.1-0f6cbd.svg" alt="Version">
  <img src="https://img.shields.io/badge/Maturity-functional-00E5FF.svg" alt="Maturity">
</p>

---

**诚实性检查 - 今天真正能运行的部分:** 核心（PZEM-004T v3 和 PZEM-017 电表的帧、轮询它们的总线、节点的消息、模拟输入和切换规则：16 个测试程序中的 379,581 项检查，其中 375,008 项针对切换规则）在计算机上用替身电表和接触器测试，它生成的消息被 ARMOR-COMMON 接受。节点固件（设置、串行线路、面板的电表和读数页面、蓝牙、签名更新）可在 ESP-IDF 5.4 容器中为两种板子构建。**它从未在板上运行，没有连接任何电表或接触器，这里的任何东西都不会切换任何设备。**

---

## 📋 速览

| | |
|---|---|
| 版本 | `0.4.1` |
| 成熟度 | 可用 |
| 测试 | 379,581 checks in 16 programs (`ctest`) |
| 技术栈 | C++17 (host-tested core); ESP-IDF firmware for the ESP32-S3 (Wi-Fi or Ethernet), with its web panel |
| 真实硬件 | 暂无：没有制作板子，也没有连接电表或接触器；不会切换任何设备 |

## 🎯 概述

* **读取电表：** Peacefair **PZEM-004T v3**（交流）和 **PZEM-017**（直流）的 Modbus RTU 帧：CRC、读寄存器的请求以及应答的解码（电压、电流、功率、电能、频率、功率因数、报警）。只构造读请求：向电表写入的请求（地址、阈值、电能清零）在任何地方都不构造。不匹配的应答，或包含任何真实电表都给不出的数值的应答，会被拒绝。
* **总线：** 带多个电表的线路逐个轮询并带超时；保留每个电表最后一次正常读数，静默的电表在十秒后不再发布。
* **消息** `armor/electrical/<节点>/state`：每个通道（一条回路、一条线路、电网入口、一条直流母线）一项，含交流或直流、电压、电流、功率、电能、频率、功率因数、节点所见的开关状态和报警；它属于共享契约，只带状态，绝不带命令。
* **开关规则，** 与任何硬件无关：两个电源之间转换的控制器，绝不同时指令两者，要求开关操作被允许、节点在刚才已布防、两个接触器在整个死区时间内都被确认断开，并把没有显示所指令状态的接触器当作故障，直到被确认才消除。**开关默认关闭，没有任何硬件被驱动。**
* **对开关的命令（未启用）：** 状态消息可以携带节点各开关的状态，共享契约中有一条命令（`arm`，然后带一次性令牌的 `close_a` 或 `close_b`、`open`、`acknowledge`）以及节点的应答；核心按上述规则读取并应答，已用共享向量和模拟接触器测试。没有任何固件镜像包含它，因此不能驱动任何接触器；除非已启用（`ARMOR_ELECTRICAL_SWITCHING=1`）且代理的 ACL 允许，ARMOR-SERVER 不会发送命令。
* **在哪里显示：** ARMOR-STUDIO 的*电气设计器*绘制住宅的电网，并在每个元件上显示其通道的测量值；ARMOR-SERVER 保存读数、历史和合计。
* **节点固件**（Wi-Fi 的 ESP32-S3-WROOM-1 N16R8，或有线的 Waveshare ESP32-S3-ETH）：设置、供最多十六块电表使用的一条串口线路，以及与其他节点相同的网页面板（设置、用户、Wi-Fi、代理、无线更新、HTTPS），另有自己的电表页和读数页（八种语言）。只读取：开关规则没有链接进去。参见[固件](docs/NODE_FIRMWARE.md)。
* **通过蓝牙用手机配置：** 与雷达节点相同的通道。ARMOR 应用会以 `ARMOR-XXXXXX` 找到节点，并使用面板的用户和设置码来设置其名称、Wi-Fi、地址、代理和蓝牙模式（[协议](docs/BLE_PROVISIONING.md)）。除非另行设置，它只在节点还没有用户时监听。无线部分从未在电路板上运行过。
* **签名的无线更新：**节点只会从 GitHub 安装 SHA-256 匹配且带有维护者有效 Ed25519 签名的镜像（公钥在固件中，私钥从不离开维护者）；管理员在面板中手动上传的文件仍然被接受。面板的页面和消息有八种语言。
* **带 PIN 的蓝牙配对：**六位 PIN 由设置码派生，打印在 USB 控制台上并向管理员显示在面板中；手机必须输入它才能写入节点的设置。
* **尚未完成：** 在电路板上运行的固件、硬件、对真实设施的任何测量以及任何开关操作。
* **只有家中才能使用面板：**面板默认只响应私有网络（或所有者列出的网络，如 VPN），拒绝会把保存者自己挡在外面的列表，按人和按地址统计错误密码，并可在服务器代理之后工作，从而无需公开节点就能在家外访问。**估算值会标明：** 由电流输入生成的通道带有 `power_estimated`，Studio 和服务器会把它的功率显示为估算值（由电流和额定电压得出的视在功率），并标记包含它的电网数值。
* **可选的与代理加密连接(TLS):** 默认关闭,因此节点仍像以前一样使用未加密的 MQTT。代理页面中的一个开关和一个用于填写本安装自有证书颁发机构证书(PEM,最大 2 KB)的框,会让节点用 `mqtts://` 连接、只信任该机构,并检查代理证书是否为其所到达的地址而签发;不是单个 PEM 证书的内容会被拒绝。已有主机测试覆盖;握手尚未在电路板上运行。
* **健康报告：** 节点大约每分钟发布一次：已运行多久、上次为何重启、可用内存、连接与 Wi-Fi 信号、连接代理的次数以及记录的最后一个错误（`armor/health/<节点>`，即 ARMOR-COMMON 的 `node_health` 契约）。生成它的代码由所有节点共享并在计算机上测试；尚未在开发板上运行。

## 🖼️ 屏幕截图

在测试台上以 1920×1080 截取，使用示例数据，背后没有真实的安装。

<table>
<tr><td width="50%" valign="top"><img src="images/screenshots/overview.png" alt="概览：节点状态一目了然——网络、代理、固件及其上报内容。" width="100%"><br><sub>概览：节点状态一目了然——网络、代理、固件及其上报内容。</sub></td><td width="50%" valign="top"><img src="images/screenshots/meters.png" alt="电表：节点读取的电能表和模拟通道，各有名称和量程。" width="100%"><br><sub>电表：节点读取的电能表和模拟通道，各有名称和量程。</sub></td></tr>
<tr><td width="50%" valign="top"><img src="images/screenshots/readings.png" alt="读数：节点此刻读取到的内容，以及每个数值的时效。" width="100%"><br><sub>读数：节点此刻读取到的内容，以及每个数值的时效。</sub></td><td width="50%" valign="top"><img src="images/screenshots/zigbee.png" alt="Zigbee 无线电：通往协调器模块的串口桥接、允许使用它的客户端以及桥接状态。" width="100%"><br><sub>Zigbee 无线电：通往协调器模块的串口桥接、允许使用它的客户端以及桥接状态。</sub></td></tr>
</table>

## 🧩 在 A.R.M.O.R. 中的角色

ARMOR-ELECTRICAL 通过电表测量住宅的交流和直流网络，并保存在电源之间切换的规则。它把读数发布给 [ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)，由后者保存历史和每日能量并触发报警；[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO) 的*电气*菜单和*电气设计器*以及 [ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL) 的电气界面显示这些内容。切换已经设计好（预备、一次性令牌、互锁、两个接触器的确认）但未启用。

## 📂 仓库结构

```text
ARMOR-ELECTRICAL/
├── main/    the ESP-IDF component: app_main, electrical_manager (the task), uart_bus, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    pzem (frames of the PZEM meters), pzem_bus (the line), meter_runner (one turn of the loop), electrical_json (the message), electrical_config (the settings),
│            interlock (the rules for switching, NOT linked into the firmware), ble_frame + ble_dispatch (Bluetooth), auth, netplan, board_s3, json...
├── panel/   the web panel: index.html, app.js, text.js (8 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_meters, test_runner, test_config, test_mqtt_tls, test_ble, test_interlock, emit_samples + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, NODE_FIRMWARE, BLE_PROVISIONING, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ 开发环境

**环境要求:** a C++17 compiler for the host tests, the ESP-IDF 5.4 container to build the image, an ESP32-S3-WROOM-1 N16R8 or a Waveshare ESP32-S3-ETH

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 379,581 checks: the meters (62), the loop that asks them (72), the settings (85), the readings and inputs (477), Bluetooth (68), the shared base (252), the central link (19), the release and signature checks (20), the interlock (135,736) and the switch sets (239,272)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

参见[设计](docs/DESIGN.md)、[安全说明](docs/SAFETY.md)、[开关规则](docs/SWITCHING.md)、[协议](docs/PROTOCOLS.md)和[消息](docs/ELECTRICAL_MESSAGES.md)。

## 🔗 相关项目

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）是由若干独立仓库组成的周界安防系统。每个仓库都有自己的版本、测试和 README；家族成员如下：

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - 消息契约、验证器、一致性向量和生成的类型
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - 适用于 ESP32-S3 的现场节点固件，带三个雷达和自带网页面板
* **[ARMOR-SOLAR](https://github.com/JuanenRac/ARMOR-SOLAR)** - 太阳能逆变器与电池的协议，以及网关节点的消息
* **ARMOR-ELECTRICAL** (本仓库) - 电气节点：电表、电网读数消息和开关规则
* **[ARMOR-ALARM](https://github.com/JuanenRac/ARMOR-ALARM)** - 报警节点与报警主机：防区、布防、延时、警笛和 PIN，有无服务器均可
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - 触摸面板：墙面屏幕上的系统状态、布防与确认，以及语音助手的所在
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - 本地网络：其设备、互联网以及变化
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - 中央协调器：遥测、报警、设备、太阳能读数和摄像头
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - 网页控制台：摄像头、雷达、报警、太阳能和 2D/3D 场地设计器
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - 带实时 2D/3D 雷达的 Android 操作员客户端
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - 会解释决策且从不执行动作的视觉推理策略
* **[ARMOR-VISION](https://github.com/JuanenRac/ARMOR-VISION)** - Jetson Orin NX 上的摄像头区域、遮罩与检测规则
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - 带无法伪造确认的离线语音意图
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - 外壳、电子器件和台架验收矩阵
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - 部署、CM5 测试台、备份与 TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - 带可重复故障的离线遥测模拟器
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - 发现、安装并更新生态系统自身的仓库
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - 架构、安全基线和能力矩阵

## 📚 文档与社区

更多阅读：

* [能力矩阵：哪些已被证实，哪些没有](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [项目目录：版本以及各仓库之间的依赖](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [本仓库的变更记录](CHANGELOG.md)
* [许可证（GPL-3.0-or-later）](LICENSE)
* 问题、想法与反馈：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 许可证

GPL-3.0-or-later - 见 [LICENSE](LICENSE)。
