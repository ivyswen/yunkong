#pragma once
// OneNET AIoT (物联网融合使能平台) OneJson 物模型协议支持
// 纯 C/C++ 逻辑，零动态内存分配，可直接用 g++ 编译单测，可在 ESP8266/ESP-12F 上运行。
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>

// 构造 OneNET 属性下发订阅主题: $sys/{pid}/{devName}/thing/property/set
static inline int onenetBuildSetTopic(char* buf, size_t bufSize, const char* pid, const char* devName) {
  if (!buf || !pid || !devName || !pid[0] || !devName[0]) return -1;
  int n = snprintf(buf, bufSize, "$sys/%s/%s/thing/property/set", pid, devName);
  if (n < 0 || (size_t)n >= bufSize) return -1;
  return n;
}

// 构造 OneNET 属性下发响应主题: $sys/{pid}/{devName}/thing/property/set_reply
static inline int onenetBuildSetReplyTopic(char* buf, size_t bufSize, const char* pid, const char* devName) {
  if (!buf || !pid || !devName || !pid[0] || !devName[0]) return -1;
  int n = snprintf(buf, bufSize, "$sys/%s/%s/thing/property/set_reply", pid, devName);
  if (n < 0 || (size_t)n >= bufSize) return -1;
  return n;
}

// 构造 OneNET 属性上报发布主题: $sys/{pid}/{devName}/thing/property/post
static inline int onenetBuildPostTopic(char* buf, size_t bufSize, const char* pid, const char* devName) {
  if (!buf || !pid || !devName || !pid[0] || !devName[0]) return -1;
  int n = snprintf(buf, bufSize, "$sys/%s/%s/thing/property/post", pid, devName);
  if (n < 0 || (size_t)n >= bufSize) return -1;
  return n;
}

// 从 JSON 串中提取 "id" 字段的值（支持字符串类型 "id":"123" 或数字类型 "id":123）
static inline bool onenetExtractId(const char* json, char* outId, size_t outIdSize) {
  if (!json || !outId || outIdSize < 2) return false;
  outId[0] = '\0';
  const char* p = strstr(json, "\"id\"");
  if (!p) return false;
  p += 4;
  while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) {
    p++;
  }
  if (!*p) return false;
  
  size_t idx = 0;
  if (*p == '"') {
    p++;
    while (*p && *p != '"' && idx < outIdSize - 1) {
      outId[idx++] = *p++;
    }
  } else {
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-') && idx < outIdSize - 1) {
      outId[idx++] = *p++;
    }
  }
  outId[idx] = '\0';
  return idx > 0;
}

// 解析 OneNET 属性下发报文 (thing/property/set)
// 查找 switch1 或 total_switch 属性
// 返回: 1=开, 0=关, -1=未找到有效开关指令
static inline int onenetParseSetPayload(const uint8_t* payload, unsigned int len, char* outId, size_t outIdSize) {
  if (!payload || len == 0) return -1;
  
  // 保证有一个 null-terminated 视图进行字符串查找
  char tmp[512];
  if (len >= sizeof(tmp)) {
    // 超过缓冲区截断
    memcpy(tmp, payload, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
  } else {
    memcpy(tmp, payload, len);
    tmp[len] = '\0';
  }
  
  if (outId && outIdSize > 0) {
    if (!onenetExtractId(tmp, outId, outIdSize)) {
      strncpy(outId, "1", outIdSize);
      outId[outIdSize - 1] = '\0';
    }
  }
  
  // 查找 switch1 或 total_switch
  const char* sw = strstr(tmp, "\"switch1\"");
  if (!sw) {
    sw = strstr(tmp, "\"total_switch\"");
  }
  if (!sw) return -1;
  
  // 找到冒号
  const char* colon = strchr(sw, ':');
  if (!colon) return -1;
  colon++;
  while (*colon && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n')) {
    colon++;
  }
  
  if (strncmp(colon, "true", 4) == 0 || *colon == '1') {
    return 1;
  }
  if (strncmp(colon, "false", 5) == 0 || *colon == '0') {
    return 0;
  }
  // 部分第三方下发可能带引号 "true", "false", "on", "off"
  if (*colon == '"') {
    colon++;
    if (strncmp(colon, "true", 4) == 0 || strncmp(colon, "on", 2) == 0 || *colon == '1') {
      return 1;
    }
    if (strncmp(colon, "false", 5) == 0 || strncmp(colon, "off", 3) == 0 || *colon == '0') {
      return 0;
    }
  }
  return -1;
}

// 构造属性设置响应报文 (thing/property/set_reply): {"id":"xxx","code":200,"msg":"success"}
static inline int onenetBuildSetReply(char* buf, size_t bufSize, const char* id, int code, const char* msg) {
  if (!buf || !id || !msg) return -1;
  int n = snprintf(buf, bufSize, "{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\"}", id, code, msg);
  if (n < 0 || (size_t)n >= bufSize) return -1;
  return n;
}

// 构造属性上报报文 (thing/property/post): {"id":"xxx","version":"1.0","params":{"switch1":{"value":true},"total_switch":{"value":true}}}
static inline int onenetBuildPropertyPost(char* buf, size_t bufSize, const char* id, int flagState) {
  if (!buf || !id) return -1;
  const char* boolStr = (flagState == 1) ? "true" : "false";
  int n = snprintf(buf, bufSize,
    "{\"id\":\"%s\",\"version\":\"1.0\",\"params\":{\"switch1\":{\"value\":%s},\"total_switch\":{\"value\":%s}}}",
    id, boolStr, boolStr);
  if (n < 0 || (size_t)n >= bufSize) return -1;
  return n;
}
