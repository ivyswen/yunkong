# 自定义 MQTT & OneNET AIoT 固件

在最新版固件（`../12F/12F.ino`）基础上增加**双模式支持**：
1. **OneNET AIoT 物联网融合平台**：支持 TLS 8883 (MQTTS) 加密接入，采用官方 OneJSON 物模型协议，支持双向状态同步与云端开关控制，配网动态填入 Token（不硬编码密钥，保障开源安全）。
2. **自定义局域网 MQTT 服务器**：脱离云端后台，直连自建 broker（如 CasaOS Mosquitto），用任意 MQTT 客户端（mqtt-panel、Home Assistant 等）控制。
3. **两模式通用能力**：设备本地定点时刻表（断网照常执行）、断电记忆、防重投窗口、按键本地控制。

## 配网字段说明（访问 192.168.22.1 配网页）

| 字段 | 含义 |
|---|---|
| WiFi 名称/密码 | 2.4G WiFi 的 SSID 与密码 |
| 设备名称 | OneNET 模式下为设备名（默认 `ykyc001_dev1`）；自定义模式下为 topic 一段 |
| OneNET Token | 连接鉴权密码（用 `tools/onenet_tool.py` 生成后填入；自定义模式留空） |
| **服务器** | **填了即启用自定义模式**（broker 地址，如 `192.168.1.10`）；留空 = OneNET AIoT TLS 8883 |
| 端口 | 留空 = 8883 (OneNET) / 1883 (自定义 MQTT) |
| 产品ID/用户名 | OneNET 模式下为产品 ID (PID，默认 `MiXfbNnY6r`)；自定义模式下为 MQTT 用户名（可留空） |
| 密码 | 仅自建 MQTT 使用；OneNET 模式留空 |

改配置：长按按键 10 秒清配网重配，或开设备局域网 IP 的维护页用「清除配网并重启」。状态页 `/status` 的 `mqtt_mode` 字段会显示 `custom` / `onenet`。

---

## OneNET AIoT 快速接入指南

### 1. 计算 Token（安全无泄露）
使用仓库自带的调试工具计算 10 年免维护 Token（密钥仅保留在本地终端，不入库）：

```bash
# 设备级 Token（推荐，仅授权该设备）
python tools/onenet_tool.py token --pid MiXfbNnY6r --dev ykyc001_dev1 --key <你的AccessKey>

# 或产品级 Token（该产品下所有插座通用）
python tools/onenet_tool.py token --pid MiXfbNnY6r --key <你的AccessKey>
```

### 2. 注册设备（如未注册）
```bash
python tools/onenet_tool.py create --pid MiXfbNnY6r --name ykyc001_dev1 --key <你的AccessKey>
```

### 3. 设备配网
1. 固件刷入插座（整 bin 写 `0x00000`）。
2. 手机连接插座热点（`YKYC-xxxxxx`），打开浏览器访问 `http://192.168.22.1`。
3. 填入：
   - WiFi 名称与密码
   - 设备名称：`ykyc001_dev1`
   - OneNET Token：粘贴刚才计算出的 `version=2018-10-31&...` 字符串
   - 服务器、端口、产品ID、密码全部留空（自动默认 PID `MiXfbNnY6r` 与 TLS 8883）。
4. 点击「开始配网」，设备自动重启并通过 TLS 8883 连接 OneNET AIoT。

### 4. 物模型通信协议（OneJSON）
- **属性下发（云端 -> 设备）**：`$sys/{pid}/{devName}/thing/property/set`
  - 格式：`{"id":"123","version":"1.0","params":{"switch1":true}}`（支持 `switch1` 与 `total_switch`）
- **下发响应（设备 -> 云端）**：`$sys/{pid}/{devName}/thing/property/set_reply`
  - 格式：`{"id":"123","code":200,"msg":"success"}`
- **属性上报（设备 -> 云端）**：`$sys/{pid}/{devName}/thing/property/post`
  - 格式：`{"id":"1","version":"1.0","params":{"switch1":{"value":true},"total_switch":{"value":true}}}`

---

## 定点时刻表（本地执行）

设备本地存最多 **4 条**规则，到点自己动作，断网照常执行。规则 = 时刻 + 开/关 + 星期掩码。

**文本格式**：`HHMM,动作,掩码`，多条用 `;` 分隔
- 例：`0730,1,127;1730,0,127` = 每天 07:30 开、17:30 关
- 动作：`1`=开，`0`=关；时刻也可写 `07:30` 形式
- 掩码：bit0=周一 … bit6=周日；`127`=每天，`31`=工作日（周一至五），`96`=周末
- 空文本 = 清空全部；任一条格式非法 → 整表拒绝、保持原表

**配置入口**：
1. 局域网维护页（设备 IP 的 OTA 页）：「定时开关」表单直接填，含时区偏移（默认 UTC+8）
2. MQTT（自定义模式）：向 `yunkong/<设备ID>/schedule/set` 发布上述文本整表替换；设备把生效中的时刻表 retained 回发到 `yunkong/<设备ID>/schedule`。

---

## 局域网自建 MQTT 协议

以设备ID = `socket1` 为例：

| 方向 | Topic | 载荷 | 说明 |
|---|---|---|---|
| 控制 ↓ | `yunkong/socket1/set` | `ON`/`OFF`（也认 `1`/`0`、单字节 `0x01`/`0x00`） | QoS 1 订阅 |
| 状态 ↑ | `yunkong/socket1/state` | `ON`/`OFF` | retained；连接时、每次状态变化时上报（含物理按键切换） |
| 在线 ↑ | `yunkong/socket1/online` | `online`/`offline` | retained；offline 由 LWT 遗嘱发布 |

---

## 限制（先说清）

- 自定义模式只走**明文 TCP MQTT**（局域网 broker 的常规用法），**无 TLS**，仅适合可信局域网；需要 TLS 的公网 broker 不在此版范围。OneNET AIoT 模式为 TLS 8883 + 内置根证书校验，不受影响。
- 防 retained 旧指令抵消：设备每次（重）订阅后的 **2.5 秒窗口内**收到的 `set` 只清不执行（判定为 broker 重投的 retained/排队包），且设备收到 `set` 后会向同一 topic 发布空的 retained 消息、清掉 broker 上的残留。代价：恰好在重连瞬间 2.5 秒内发出的正常指令也会被吞，看 `state` 没变重发即可。开关状态以 retained 的 `state` 为准，面板的 `set` 不必勾 retain。
- 设备 ID 只允许 `A-Z a-z 0-9 _ -`（1–32 位）；含 `/`、`+`、`#`、空格、引号等的会被回落为默认设备名（串口的 `Mode:` 行会打印实际使用的名字）。
- 旧版 OneNET（产品 538578 + 作者小程序 + cmd 协议）已整体移除：旧配网参数（如旧 sign）升级后不再适用，必须按上面的 AIoT 流程重新配网。
- OneNET 协议的「延时恢复」（倒计时式延时开关）未移植到自定义模式；定点时刻表是设备本地实现，双模式可用。

## 编译与测试

WSL 编译命令（使用 arduino-cli）：
```bash
rduino-cli compile -b 'esp8266:esp8266:generic:xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=basic,mmu=3232,non32xfer=fast,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=dout,eesz=1M64,led=2,sdk=nonosdk_190703,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=115200' custom-mqtt/12F
```

主机侧单测（无需硬件，直接用 g++ 验证协议解析）：
```bash
g++ -Wall -Wextra -O2 custom-mqtt/test/test_onenet_aiot.cpp -o test_onenet && ./test_onenet
g++ -Wall -Wextra -O2 custom-mqtt/test/test_mqtt_custom.cpp -o test_custom && ./test_custom
g++ -Wall -Wextra -O2 custom-mqtt/test/test_schedule.cpp -o test_sched && ./test_sched
```
