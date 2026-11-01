# yunkong — 浙江云控 YKYC-001 智能插座复活固件

阿里智能停服后，YKYC-001（ESP-12F / ESP8266）插座的自写固件：脱离阿里云，改连中国移动 OneNET，用微信小程序远程控制。

固件原作者：MyYJjava（Gitee: `jjiangajava/yun_kong`，CSDN 教程《阿里智能YKYC-001智能插座，残血复活》《运控智能插座不能用了，自己写代码解决》）。本仓库是存档 + 复现编译：上游仓库现行目录已删除源码，本仓库的源码从其 git 历史中找回，并已在本地按作者配置复现编译，与作者发布的 bin 校验等价。

## 目录

| 路径 | 说明 |
|---|---|
| `12F/12F.ino` | 最新版固件源码（661 行）：延时修复、启动超时修复、本地状态页 |
| `legacy/12F_v1.ino` | 第一版源码（381 行），即 CSDN 源码篇发布版 |
| `legacy/12F_ESP01.ino` | ESP-01 变体源码（602 行，参考） |
| `bin/ykyc001_v2_compiled.bin` | 最新版本地编译产物，462,896 B |
| `bin/ykyc001_v1_compiled.bin` | v1 本地编译产物，457,520 B |
| `bin/author/` | 作者发布的原版 bin（v1 / v2 / esp01_v2） |
| `FLASHING.md` | 刷机说明：备份原厂、接线、刷入、配网 |
| `custom-mqtt/` | 自定义 MQTT 变体固件：配网页可填自己的 broker，脱离作者 OneNET（见其 README）；产物 `bin/ykyc001_v2_custom_mqtt.bin` |

## 硬件要点

- 模块：ESP-12F V2.0；引脚：蓝灯 GPIO12、红灯 GPIO16、继电器 GPIO14、按钮 GPIO13
- 刷写焊盘 1–5：VCC 3.3V / GND / TX / RX / 接 GND 进刷写模式
- 后台：OneNET MQTTS `mqttstls.heclouds.com:8883`，产品 ID `538578`（作者托管，服务端源码未公开）

## 编译

- arduino-cli + ESP8266 core 3.1.2
- 库：BGWiFiConfig 1.1.1（fish-five）、PubSubClient
- 板型配置（作者 arduino.json 原样）：`esp8266:esp8266:generic`，xtal=80、CrystalFreq=26、FlashFreq=40、FlashMode=dout、eesz=1M64、ssl=basic、mmu=3232、non32xfer=fast、ResetMethod=nodemcu、sdk=nonosdk_190703、ip=lm2f、led=2、vt=flash、exception=disabled、stacksmash=disabled

```bash
arduino-cli compile -b 'esp8266:esp8266:generic:xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=basic,mmu=3232,non32xfer=fast,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=dout,eesz=1M64,led=2,sdk=nonosdk_190703,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=115200' 12F
```

校验：自编 v2（462,896 B）与作者 v2 bin（462,912 B）仅差 16 B 构建信息，可读字符串全等。

## 配网

上电灯闪 → 手机连「配网」热点 → 浏览器 192.168.22.1 填家里 2.4G WiFi、设备 ID（小程序「复制 ID」）、加密串（「复制参数」）、mode=1。长按按键 10 秒清配网重来。

⚠️ 250V 市电设备：串口刷机全程禁止插市电，只用隔离 3.3V 供电；刷前先 `read_flash` 整片备份原厂固件（见 FLASHING.md）。
