# 自定义 MQTT & OneNET AIoT 固件

在最新版固件（`../12F/12F.ino`）基础上增加**双模式支持**：
1. **OneNET AIoT 物联网融合平台**：支持 TLS 8883 (MQTTS) 加密接入，采用官方 OneJSON 物模型协议，支持双向状态同步与云端开关控制，配网动态填入 Token（不硬编码密钥，保障开源安全）。
2. **自定义局域网 MQTT 服务器**：脱离云端后台，直连自建 broker（如 CasaOS Mosquitto），用任意 MQTT 客户端（mqtt-panel、Home Assistant 等）控制。
3. **两模式通用能力**：断电记忆、防重投窗口、按键本地控制。

## 配网字段说明（访问 192.168.22.1 配网页）

| 字段 | 含义 |
|---|---|
| WiFi 名称/密码 | 2.4G WiFi 的 SSID 与密码 |
| 设备名称 | OneNET 模式下为设备名（默认 `ykyc001_dev1`）；自定义模式下为 topic 一段 |
| OneNET Token | 连接鉴权密码（用 `tools/onenet_tool.py` 生成后填入；自定义模式留空） |
| **服务器** | **填了即启用自定义模式**（broker 地址，如 `192.168.1.10`）；留空 = OneNET AIoT TLS 8883。只填裸 IP/域名，**不要带 `mqtt://` 前缀**，带前缀连不上 |
| 端口 | 留空 = 8883 (OneNET) / 1883 (自定义 MQTT) |
| 产品ID/用户名 | OneNET 模式下为产品 ID (PID，默认 `MiXfbNnY6r`)；自定义模式下为 MQTT 用户名（可留空） |
| 密码 | 仅自建 MQTT 使用；OneNET 模式留空 |

改配置：按住按键 3 秒以上松开清配网重配（按满 10 秒同样有效），或开设备局域网 IP 的维护页用「清除配网并重启」。状态页 `/status` 的 `mqtt_mode` 字段会显示 `custom` / `onenet`。

> **真机状态（2026-11-03）**：自定义 MQTT 模式已在实机验证（配网、连接自建 Mosquitto、HA 控制均正常）；OneNET AIoT 的 TLS 握手尚未真机验证。另：自定义模式下设备不做 NTP 对时（时钟仅 OneNET TLS 路径用），状态页 NTP 显示 NO 属正常现象，不是故障。

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

> 电脑联调：`python tools/onenet_tool.py test-mqtt --help` 可模拟设备连接平台收发物模型消息（TLS 用随仓 `tools/onenet_root_ca.pem` 做根证书校验，与固件同源）。
```

### 3. 设备配网
1. 固件刷入插座（整 bin 写 `0x00000`）。
2. 手机连接插座热点（`WiFi配网`，BGWiFiConfig 默认名），打开浏览器访问 `http://192.168.22.1`。
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

## 局域网自建 MQTT 协议

以设备ID = `socket1` 为例：

| 方向 | Topic | 载荷 | 说明 |
|---|---|---|---|
| 控制 ↓ | `yunkong/socket1/set` | `ON`/`OFF`（也认 `1`/`0`、单字节 `0x01`/`0x00`） | QoS 1 订阅 |
| 状态 ↑ | `yunkong/socket1/state` | `ON`/`OFF` | retained；连接时、每次状态变化时上报（含物理按键切换） |
| 在线 ↑ | `yunkong/socket1/online` | `online`/`offline` | retained；offline 由 LWT 遗嘱发布 |

### Home Assistant 接入（已真机实测）

HA 的 MQTT 集成连上同一个 broker 后，在 `configuration.yaml` 加手动 MQTT 开关（设备 ID 换成自己的；定时开关用 HA 自动化按时间触发 `switch.turn_on/off` 实现）：

```yaml
mqtt:
  switch:
    - name: "云控插座"
      unique_id: ykyc001_dev1_switch
      command_topic: "yunkong/ykyc001_dev1/set"
      state_topic: "yunkong/ykyc001_dev1/state"
      availability_topic: "yunkong/ykyc001_dev1/online"
      payload_on: "ON"
      payload_off: "OFF"
      payload_available: "online"
      payload_not_available: "offline"
      state_on: "ON"
      state_off: "OFF"
      retain: false
```

---

## 限制（先说清）

- 自定义模式只走**明文 TCP MQTT**（局域网 broker 的常规用法），**无 TLS**，仅适合可信局域网；需要 TLS 的公网 broker 不在此版范围。OneNET AIoT 模式为 TLS 8883 + 内置根证书校验，不受影响。
- 防 retained 旧指令抵消：设备每次（重）订阅后的 **2.5 秒窗口内**收到的 `set` 只清不执行（判定为 broker 重投的 retained/排队包），且设备收到 `set` 后会向同一 topic 发布空的 retained 消息、清掉 broker 上的残留。代价：恰好在重连瞬间 2.5 秒内发出的正常指令也会被吞，看 `state` 没变重发即可。开关状态以 retained 的 `state` 为准，面板的 `set` 不必勾 retain。
- 设备 ID 只允许 `A-Z a-z 0-9 _ -`（1–32 位）；含 `/`、`+`、`#`、空格、引号等的会被回落为默认设备名（串口的 `Mode:` 行会打印实际使用的名字）。
- 旧版 OneNET（产品 538578 + 作者小程序 + cmd 协议）已整体移除：旧配网参数（如旧 sign）升级后不再适用，必须按上面的 AIoT 流程重新配网。
- OneNET 协议的「延时恢复」（倒计时式延时开关）未移植到自定义模式。

## 编译与测试

WSL 编译命令（使用 arduino-cli）：
```bash
arduino-cli compile -b 'esp8266:esp8266:generic:xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=basic,mmu=3232,non32xfer=fast,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=dout,eesz=1M64,led=2,sdk=nonosdk_190703,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=115200' custom-mqtt/12F
```

主机侧单测（无需硬件，直接用 g++ 验证协议解析）：
```bash
g++ -Wall -Wextra -O2 custom-mqtt/test/test_onenet_aiot.cpp -o test_onenet && ./test_onenet
g++ -Wall -Wextra -O2 custom-mqtt/test/test_mqtt_custom.cpp -o test_custom && ./test_custom
python3 custom-mqtt/test/test_provisioning_html.py
```
