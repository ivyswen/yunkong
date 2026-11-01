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
      break;       // 正文后的空白即结束
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
