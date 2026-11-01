#pragma once
// 自定义 MQTT 模式的纯逻辑：无 Arduino 依赖，主机侧可直接编译测试。
// 协议形状（见 custom-mqtt/README.md）：
//   订阅  yunkong/<设备ID>/set     载荷 ON/OFF/1/0 或单字节 0x01/0x00
//   上报  yunkong/<设备ID>/state   retained, ON/OFF
//   在线  yunkong/<设备ID>/online  retained, online/offline(LWT)
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// 解析开关载荷：返回 1=开, 0=关, -1=无法识别（忽略该消息）
static inline int ykParseSwitchPayload(const uint8_t* p, unsigned int len) {
  if (p == NULL || len == 0) {
    return -1;
  }
  // 单字节二进制载荷（字节型面板直接发 0x00/0x01）
  if (len == 1 && (p[0] == 0x00 || p[0] == 0x01)) {
    return p[0];
  }
  // 文本载荷：去首尾空白后整体比对（大小写不敏感）
  char tmp[8];
  unsigned int n = 0;
  for (unsigned int i = 0; i < len; i++) {
    if (isspace((unsigned char)p[i])) {
      if (n == 0) {
        continue;  // 跳过前导空白
      }
      // 正文后遇到空白：其后只允许空白，否则整条无法识别（防 "ON garbage" 被当成 ON）
      for (unsigned int j = i + 1; j < len; j++) {
        if (!isspace((unsigned char)p[j])) {
          return -1;
        }
      }
      break;
    }
    if (n >= sizeof(tmp) - 1) {
      return -1;   // 过长不可能是合法指令
    }
    tmp[n++] = (char)toupper((unsigned char)p[i]);
  }
  tmp[n] = '\0';
  if (strcmp(tmp, "ON") == 0 || strcmp(tmp, "1") == 0) {
    return 1;
  }
  if (strcmp(tmp, "OFF") == 0 || strcmp(tmp, "0") == 0) {
    return 0;
  }
  return -1;
}

// 构造 topic：yunkong/<id>/<leaf>，返回写入长度（不含结尾 0），buf 不足时返回 -1
static inline int ykBuildTopic(char* buf, size_t bufSize, const char* id, const char* leaf) {
  if (buf == NULL || id == NULL || leaf == NULL || id[0] == '\0' || leaf[0] == '\0') {
    return -1;
  }
  int n = snprintf(buf, bufSize, "yunkong/%s/%s", id, leaf);
  if (n < 0 || (size_t)n >= bufSize) {
    return -1;
  }
  return n;
}

// 设备 ID 合法性：用作 MQTT topic 段与 JSON/HTML 输出，只允许 [A-Za-z0-9_-]，长度 1..32。
// 拒绝 '/', '+', '#'（会让订阅变成通配）以及引号、尖括号等（会破坏 /status JSON、维护页 XSS）。
static inline bool ykDeviceIdValid(const char* id) {
  if (id == NULL) {
    return false;
  }
  size_t n = strlen(id);
  if (n == 0 || n > 32) {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)id[i];
    if (!(isalnum(c) || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

// 订阅后忽略窗口判定（与 OneNET 分支的 (long)(millis()-until)>=0 放行互为镜像，millis 环绕安全）：
// now 还没到 until => 处于窗口内。retained 的 set 只会在订阅瞬间被重投，窗口内的 set 只清不执行，
// 避免旧指令抵消断线期间时刻表/按键已造成的继电器状态；until=0（未布防）恒为窗口外。
static inline bool ykWithinIgnoreWindow(unsigned long nowMs, unsigned long untilMs) {
  // 先截到 32 位再看符号：millis 本就是 32 位环绕量，这样写在 64 位主机测试与
  // 32 位目标机上行为一致（直接 (long)(until-now) 在主机上不回绕、会误判）。
  return (int32_t)(uint32_t)(untilMs - nowMs) > 0;
}
