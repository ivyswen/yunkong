# 自定义 MQTT 版固件

在最新版固件（`../12F/12F.ino`）基础上增加**自定义 MQTT 服务器**支持，脱离作者托管的 OneNET 后台，改连自己的 broker（如 CasaOS Mosquitto），用任意 MQTT 客户端控制（mqtt-panel、Home Assistant 等）。

原版 OneNET 路径**完全保留**：配网时不填 MQTT 服务器即走原逻辑，行为与原版一致。

## 配网字段（192.168.22.1 配网页）

| 字段 | 含义 |
|---|---|
| WiFi 名称/密码 | 家里 2.4G WiFi |
| 输入设备ID | 自定义模式下即设备名（topic 的一段）；留空自动用 `ykyc-<芯片ID>` |
| 输入加密串 | 仅 OneNET 模式用，自定义模式可留空 |
| **MQTT服务器** | **填了即启用自定义模式**（broker 地址，如 `192.168.1.10`）；留空 = 原 OneNET |
| MQTT端口 | 留空 = 1883 |
| MQTT用户名 | 可留空（匿名） |
| MQTT密码 | 可留空 |

改配置：长按按键 10 秒清配网重配，或开设备局域网 IP 的维护页用「清除配网并重启」。状态页 `/status` 的 `mqtt_mode` 字段会显示 `custom` / `onenet`。

## 定点时刻表（自定义 MQTT 模式）

设备本地存最多 **4 条**规则，到点自己动作，断网照常执行。规则 = 时刻 + 开/关 + 星期掩码。

**文本格式**（两个配置入口共用）：`HHMM,动作,掩码`，多条用 `;` 分隔

- 例：`0730,1,127;1730,0,127` = 每天 07:30 开、17:30 关
- 动作：`1`=开，`0`=关；时刻也可写 `07:30` 形式
- 掩码：bit0=周一 … bit6=周日；`127`=每天，`31`=工作日（周一至五），`96`=周末
- 空文本 = 清空全部；任一条格式非法 → 整表拒绝、保持原表

**配置入口**：

1. 局域网维护页（设备 IP 的 OTA 页）：「定时开关」表单直接填，含时区偏移（默认 UTC+8）
2. MQTT：向 `yunkong/<设备ID>/schedule/set` 发布上述文本整表替换；设备把生效中的时刻表 retained 回发到 `yunkong/<设备ID>/schedule`（上线时自动发一次）。mqtt-panel 里用文本发布面板即可改表

**存储与时间**：规则与时区存 EEPROM（偏移 128，与继电器状态的偏移 64 同域），清配网（按键长按 / 维护页清除）时一并清除。时间靠 SNTP（阿里云/cn.pool/pool.ntp.org），未对上时不会误触发；同一分钟只判定一次；触发后状态经 `state` 主题正常上报。

## 协议

以设备ID = `socket1` 为例：

| 方向 | Topic | 载荷 | 说明 |
|---|---|---|---|
| 控制 ↓ | `yunkong/socket1/set` | `ON`/`OFF`（也认 `1`/`0`、单字节 `0x01`/`0x00`，大小写/首尾空白不敏感） | QoS 1 订阅 |
| 状态 ↑ | `yunkong/socket1/state` | `ON`/`OFF` | retained；连接时、每次状态变化时上报（含物理按键切换） |
| 在线 ↑ | `yunkong/socket1/online` | `online`/`offline` | retained；offline 由 LWT 遗嘱发布 |

Client ID：`yunkong-socket1`。重连退避与原版一致（2s 起翻倍至 60s）。

### mqtt-panel 里怎么加

新建连接指向自己的 broker，新建开关面板：
- 发布 topic：`yunkong/socket1/set`，开载荷 `ON`、关载荷 `OFF`
- 订阅 topic：`yunkong/socket1/state`（同载荷），在线指示可再订阅 `yunkong/socket1/online`

## 限制（先说清）

- 自定义模式只走**明文 TCP MQTT**（局域网 broker 的常规用法）；需要 TLS 的公网 broker 不在此版范围。OneNET 模式的 TLS 不受影响。
- OneNET 协议里的「延时恢复」（倒计时式的延时开关）是作者服务端协议的一部分，自定义模式**未移植**；定点时刻表已由设备本地实现（见上）。
- 自定义模式下作者的微信小程序控制不了这台设备（它只连 OneNET）。
- retained 的 `set` 消息会在设备重连时被重新投递并执行（与面板状态同步语义一致）。

## 源码与测试

- `12F/12F.ino` — 固件（与原版的差异仅限：配网页 4 个新字段、`setNumUMSG(6)`、配置读取、回调与连接各一条自定义分支、状态页多一个字段）
- `12F/mqtt_custom.h` — 纯逻辑（载荷解析、topic 构造），无 Arduino 依赖
- `test/test_mqtt_custom.cpp` — 主机侧测试：`g++ -std=c++11 test_mqtt_custom.cpp -o test && ./test`
- 编译命令与原版相同（见仓库根 README）。产物 `../bin/ykyc001_v2_custom_mqtt.bin`，刷法同 FLASHING.md（写 0x00000）。

⚠️ 未经真机验证：已通过编译（自有文件零警告）与主机侧逻辑测试，刷机实测待硬件完成。
