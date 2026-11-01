# yunkong — 浙江云控 YKYC-001 智能插座复活固件

阿里智能停服后，YKYC-001（ESP-12F / ESP8266）智能插座的自写固件存档与增强：

- **原版路线**：连中国移动 OneNET，用作者的微信小程序远程控制（作者托管后台）
- **本仓增强（推荐）**：自定义 MQTT 版 —— 配网页直接填自己的 MQTT 服务器（如 CasaOS Mosquitto），用 mqtt-panel / Home Assistant 控制，并支持**设备本地定点时刻表**，彻底脱离作者后台

固件原作者：MyYJjava（Gitee: `jjiangajava/yun_kong`，CSDN 教程《阿里智能YKYC-001智能插座，残血复活》《运控智能插座不能用了，自己写代码解决》）。上游仓库现行目录已删除源码，本仓库的源码从其 git 历史中找回，并按作者配置复现编译、与作者发布的 bin 校验等价；自定义 MQTT 与时刻表为本仓在其最新版源码基础上的增量改动（OneNET 路径完整保留）。

## 刷哪个 bin

| 文件 | 适合 |
|---|---|
| `bin/ykyc001_v2_custom_mqtt.bin` | **推荐**。476,480 B，sha256 `2bf183337578a8515a7022428e5b28b14f409c416e8177dc9701448821485c32`。配网填 MQTT 服务器走自有 broker + 本地时刻表；不填则与原版行为一致 |
| `bin/ykyc001_v2_compiled.bin` | 原版最新源码的复现编译，462,896 B（与作者 v2 bin 仅差 16 B 构建信息） |
| `bin/author/ykyc001_v2_author.bin` | 作者发布的 v2 原版，462,912 B |
| `bin/ykyc001_v1_compiled.bin` / `bin/author/ykyc001_v1_author.bin` | 第一版（CSDN 源码篇版本），存档 |

刷法统一：整 bin 写 `0x00000`，见 `FLASHING.md`（含刷前整片备份原厂这一步，别省）。

## 自定义 MQTT 版要点（`custom-mqtt/`，详见其 README）

- 配网页新增 4 字段：MQTT 服务器 / 端口 / 用户名 / 密码。**服务器非空即启用自定义模式**，留空回落 OneNET，判定只此一处，旧配网无迁移
- 协议（设备 ID 以 `socket1` 为例）：控制 `yunkong/socket1/set`（`ON`/`OFF`，也认 `1`/`0`、单字节），状态 `yunkong/socket1/state`（retained），在线 `yunkong/socket1/online`（retained + LWT）
- 定点时刻表：本地最多 4 条规则，断网照常执行；文本形如 `0730,1,127;1730,0,127`（每天 07:30 开、17:30 关），可经局域网维护页表单或 MQTT 主题 `yunkong/<id>/schedule/set` 配置，规则存 EEPROM、SNTP 对时（默认 UTC+8）
- 限制：自定义模式仅明文 TCP（局域网定位）、无 TLS；该模式下作者微信小程序不可控；OneNET 的倒计时延时未移植

## 目录

| 路径 | 说明 |
|---|---|
| `12F/12F.ino` | 原版最新固件源码（661 行）：延时修复、启动超时修复、本地状态页 |
| `custom-mqtt/` | 自定义 MQTT 变体：源码、协议与时刻表文档、主机侧测试 |
| `legacy/12F_v1.ino` | 第一版源码（381 行），即 CSDN 源码篇发布版 |
| `legacy/12F_ESP01.ino` | ESP-01 变体源码（602 行，参考） |
| `bin/` | 各版本编译产物与作者原版 bin（见上表） |
| `FLASHING.md` | 刷机说明：备份原厂、接线、刷入、配网 |

## 硬件要点

- 模块：ESP-12F V2.0；引脚：蓝灯 GPIO12、红灯 GPIO16、继电器 GPIO14、按钮 GPIO13
- 刷写焊盘 1–5：VCC 3.3V / GND / TX / RX / 接 GND 进刷写模式
- 原版后台：OneNET MQTTS `mqttstls.heclouds.com:8883`，产品 ID `538578`（作者托管，服务端源码未公开）

## 拆机（实测方法，2026-11-01）

外壳无螺丝、合缝有胶，干撬易裂。实测有效的开法：

1. 热吹风机沿合缝均匀加热，把胶吹软
2. 用刀片沿缝撬开一条口子（刀口朝外，别往电路板和市电铜箔方向使劲）
3. 除胶液沿缝滴入，静置约 1 小时等它渗透化胶
4. 用钳子夹住一条插头脚（市电插脚）平直往外拔，整个壳体随之打开

全程断电操作；除胶液只入壳缝，不要滴到电路板上。

## 编译

- arduino-cli + ESP8266 core 3.1.2
- 库：BGWiFiConfig 1.1.1（fish-five）、PubSubClient
- 板型配置（作者 arduino.json 原样）：`esp8266:esp8266:generic`，xtal=80、CrystalFreq=26、FlashFreq=40、FlashMode=dout、eesz=1M64、ssl=basic、mmu=3232、non32xfer=fast、ResetMethod=nodemcu、sdk=nonosdk_190703、ip=lm2f、led=2、vt=flash、exception=disabled、stacksmash=disabled

```bash
arduino-cli compile -b 'esp8266:esp8266:generic:xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=basic,mmu=3232,non32xfer=fast,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=dout,eesz=1M64,led=2,sdk=nonosdk_190703,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=115200' 12F
```

校验：自编原版 v2（462,896 B）与作者 v2 bin（462,912 B）仅差 16 B 构建信息，可读字符串全等。自定义版的纯逻辑（载荷解析、topic 构造、时刻表解析/触发）有主机侧测试（`custom-mqtt/test/`，g++ 可直接跑），全绿且通过变异探针；编译自有文件零警告。

## 配网

上电灯闪 → 手机连「配网」热点 → 浏览器 192.168.22.1：

- **原版**：填家里 2.4G WiFi、设备 ID（小程序「复制 ID」）、加密串（「复制参数」）
- **自定义 MQTT**：再多填 MQTT 服务器等 4 个字段（见上），设备 ID 即设备名

长按按键 10 秒清配网重来。

⚠️ **验证状态：以上固件均只通过编译与主机侧逻辑测试，未经真机验证。** 250V 市电设备：串口刷机全程禁止插市电，只用隔离 3.3V 供电；刷前先 `read_flash` 整片备份原厂固件（见 FLASHING.md）。
