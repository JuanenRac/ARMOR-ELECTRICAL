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
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  🇯🇵 <b>日本語</b>
</p>

### 電気ノード：住宅の AC・DC ネットワークの電圧、電流、電力、エネルギーを読み取り、その開閉のルールを持ちます（PC でテスト済みのコアとビルドできるファームウェア。基板ではまだ動作していません）

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-135%2C990-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-ff9800.svg" alt="Maturity">
</p>

---

**正直さのチェック - 今日動いているもの:** **成熟度：scaffolding。** コア（PZEM-004T v3 と PZEM-017 のフレーム、それらに問い合わせるバス、ノードのメッセージ、開閉のルール：電力量計の 53 件と開閉ルールの 135,736 件のチェック）は、模擬の電力量計と接触器を使って PC でテストされ、生成するメッセージは ARMOR-COMMON に受理されます。ノードのファームウェア（設定、シリアル回線、パネルの電力量計ページと測定値ページ：さらに 71 件のチェック）は、ESP-IDF 5.4.2 コンテナで両方の基板向けにビルドできます。**基板ではまだ動作しておらず、電力量計にも接触器にも何も接続しておらず、ここでは何も開閉しません。**

---

## 🎯 概要

* **電力量計の読み取り：** Peacefair の **PZEM-004T v3**（AC）と **PZEM-017**（DC）の Modbus RTU フレーム。CRC、レジスタを読む要求、応答の解読（電圧、電流、電力、エネルギー、周波数、力率、警報）。作るのは読み取り要求だけで、電力量計に書き込むもの（アドレス、しきい値、エネルギーのリセット）はどこにも作りません。合わない応答や、実際の電力量計が出せない値を含む応答は拒否します。
* **バス：** 複数の電力量計がつながる回線には 1 台ずつタイムアウト付きで問い合わせ、それぞれの最後の正常な読み取りを保持します。応答のない電力量計は 10 秒後に公開されなくなります。
* **メッセージ** `armor/electrical/<ノード>/state`：チャンネル（回路、線、系統入力、DC バス）ごとの項目に、AC/DC、電圧、電流、電力、エネルギー、周波数、力率、ノードが見ている開閉器の状態、警報を入れます。共有契約に含まれ、状態を運び、命令は運びません。
* **開閉のルール：** ハードウェアとは別に、2 つの電源の切り替えを制御するコントローラー。両方を同時に指令せず、開閉の許可、直前のノードの武装、デッドタイム中ずっと両接触器が開と確認されていることを要求し、指令どおりの状態を示さない接触器は確認応答まで残る故障として扱います。**開閉は初期設定でオフで、どのハードウェアも駆動しません。**
* **見える場所：** ARMOR-STUDIO の*電気設計*が住宅のネットワークを描き、各要素にそのチャンネルの計測値を表示します。ARMOR-SERVER が読み取り値、履歴、合計を保持します。
* **ノードのファームウェア**（Wi-Fi の ESP32-S3-WROOM-1 N16R8、またはケーブル接続の Waveshare ESP32-S3-ETH）：設定、最大 16 台の電力量計のための 1 本のシリアル回線、他のノードと同じ Web パネル（セットアップ、ユーザー、Wi-Fi、ブローカー、無線アップデート、HTTPS）に電力量計ページと測定値ページを加えたもの（7 言語）。読み取り専用で、開閉のルールは組み込まれていません。[ファームウェア](docs/NODE_FIRMWARE.md)を参照。
* **スマートフォンから Bluetooth で設定：** レーダーノードと同じチャンネルです。ARMOR アプリがノードを `ARMOR-XXXXXX` として見つけ、パネルのユーザーとセットアップコードを使って、名前、Wi-Fi、アドレス、ブローカー、Bluetooth モードを設定します（[プロトコル](docs/BLE_PROVISIONING.md)）。設定を変えない限り、ノードにユーザーがいない間だけ待ち受けます。無線部分は基板ではまだ動作していません。
* **まだ：** 基板上で動作するファームウェア、ハードウェア、実際の設備での計測、そして開閉。

## 📂 リポジトリの構成

```text
ARMOR-ELECTRICAL/
├── main/    the ESP-IDF component: app_main, electrical_manager (the task), uart_bus, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    pzem (frames of the PZEM meters), pzem_bus (the line), meter_runner (one turn of the loop), electrical_json (the message), electrical_config (the settings),
│            interlock (the rules for switching, NOT linked into the firmware), ble_frame + ble_dispatch (Bluetooth), auth, netplan, board_s3, json...
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_meters, test_runner, test_config, test_ble, test_interlock, emit_samples + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, NODE_FIRMWARE, BLE_PROVISIONING, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ 開発環境

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 135,990 checks: the meters (53), the loop that asks them (62), the settings (71), Bluetooth (68), the switching rules (135,736)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

[設計](docs/DESIGN.md)、[安全に関する注意](docs/SAFETY.md)、[開閉のルール](docs/SWITCHING.md)、[プロトコル](docs/PROTOCOLS.md)、[メッセージ](docs/ELECTRICAL_MESSAGES.md)を参照してください。

## 🔗 関連プロジェクト

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）は、独立したリポジトリで構成される周辺警備システムです。それぞれに独自のバージョン、テスト、README があります。ファミリーは次のとおりです：

* **[ARMOR-COMMON](../ARMOR-COMMON)** - メッセージ契約、検証器、適合性ベクトル、生成された型
* **[ARMOR-RADAR](../ARMOR-RADAR)** - ESP32-S3 用フィールドノードのファームウェア。レーダー 3 基と独自の Web パネル付き
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - 太陽光インバーターとバッテリーのプロトコル、およびゲートウェイノードのメッセージ
* **ARMOR-ELECTRICAL** (このリポジトリ) - 電気ノード：電力量計、電力網の計測メッセージ、開閉のルール
* **[ARMOR-SERVER](../ARMOR-SERVER)** - 中央コーディネーター：テレメトリ、アラーム、デバイス、太陽光の測定値、カメラ
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Web コンソール：カメラ、レーダー、アラーム、太陽光発電、2D/3D サイト設計
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - リアルタイム 2D/3D レーダー付きの Android オペレータークライアント
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - 判断を説明し、決して動作しない視覚推論ポリシー
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - 偽造できない確認を備えたオフライン音声インテント
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - 筐体、電子部品、ベンチ受け入れマトリクス
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - デプロイ、CM5 テストベンチ、バックアップ、TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - 再現可能な故障を備えたオフラインのテレメトリシミュレーター
* **[ARMOR-DOCS](../ARMOR-DOCS)** - アーキテクチャ、セキュリティ基準、機能マトリクス

## 📚 ドキュメントとコミュニティ

詳しくは：

* [機能マトリクス：実証済みのものとそうでないもの](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [プロジェクト一覧：バージョンとリポジトリ間の依存関係](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [このリポジトリの変更履歴](CHANGELOG.md)
* [ライセンス（GPL-3.0-or-later）](LICENSE)
* 質問・提案・報告：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 ライセンス

GPL-3.0-or-later - [LICENSE](LICENSE) を参照。
