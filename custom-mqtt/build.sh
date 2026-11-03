#!/usr/bin/env bash
# 一键编译增强版固件，并把版本号自动注入固件：
#   FW_GIT_HASH  = 当前 git 短哈希（工作区有未提交改动时加 -dirty 后缀）
#   FW_BUILD_TIME = 本次编译时间（YYYY-MM-DDTHH:MM；用 T 分隔是因为
#                   arduino-cli 的 build-property 传参带空格会断词）
# 设备维护页状态表、配网页页脚和开机串口日志都会显示这两个值，
# OTA 后打开维护页即可确认新固件是否生效。
# 产物直接写入仓库根目录的 bin/ykyc001_v2_custom_mqtt.bin 并打印体积与 sha256。
#
# 用法：./custom-mqtt/build.sh
# 可用环境变量 ARDUINO_CLI 指定 arduino-cli 路径（默认在 PATH 中找，
# 找不到时回落 ~/bin/arduino-cli）。

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

CLI="${ARDUINO_CLI:-arduino-cli}"
if ! command -v "$CLI" >/dev/null 2>&1; then
  CLI="$HOME/bin/arduino-cli"
fi

HASH="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
  HASH="${HASH}-dirty"
fi
BUILD_TIME="$(date '+%Y-%m-%dT%H:%M')"

FQBN='esp8266:esp8266:generic:xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=basic,mmu=3232,non32xfer=fast,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=dout,eesz=1M64,led=2,sdk=nonosdk_190703,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=115200'

OUT_DIR="$(mktemp -d)"
trap 'rm -rf "$OUT_DIR"' EXIT

echo "FW version: ${HASH}  build: ${BUILD_TIME}"
"$CLI" compile --warnings all -b "$FQBN" \
  --build-property "compiler.cpp.extra_flags=-DFW_GIT_HASH=\"${HASH}\" -DFW_BUILD_TIME=\"${BUILD_TIME}\"" \
  --output-dir "$OUT_DIR" custom-mqtt/12F

cp "$OUT_DIR/12F.ino.bin" bin/ykyc001_v2_custom_mqtt.bin
echo "bin: bin/ykyc001_v2_custom_mqtt.bin"
stat -c '%s bytes' bin/ykyc001_v2_custom_mqtt.bin
sha256sum bin/ykyc001_v2_custom_mqtt.bin
