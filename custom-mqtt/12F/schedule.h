#pragma once
// 定点时刻表纯逻辑：无 Arduino 依赖，主机侧可直接编译测试。
// 规则文本（一行，规则间 ';' 分隔）：HHMM,动作,星期掩码 例 "0730,1,127;1730,0,31"
//   HHMM 也可写 HH:MM；动作 1=开 0=关；星期掩码 bit0=周一 … bit6=周日（127=每天，31=工作日，96=周末）
//   空文本 = 清空全部规则（合法）；任一规则格式非法 → 整体拒绝（返回 -1），不部分生效。
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

struct YkRule {
  uint8_t enabled;   // 0=空槽
  uint8_t hour;      // 0-23
  uint8_t minute;    // 0-59
  uint8_t action;    // 1=开 0=关
  uint8_t weekMask;  // bit0=周一 … bit6=周日
};

// 严格解析无符号整数（全为数字才算数），成功返回 true
static inline bool ykParseUint(const char* s, size_t len, int* out) {
  if (len == 0 || len > 5) {
    return false;
  }
  int v = 0;
  for (size_t i = 0; i < len; i++) {
    if (!isdigit((unsigned char)s[i])) {
      return false;
    }
    v = v * 10 + (s[i] - '0');
  }
  *out = v;
  return true;
}

// 去首尾空白后把 [s, s+len) 拷入 dst（容量 cap，含结尾 0），过长返回 false
static inline bool ykTrimCopy(char* dst, size_t cap, const char* s, size_t len) {
  while (len > 0 && isspace((unsigned char)*s)) {
    s++;
    len--;
  }
  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    len--;
  }
  if (len >= cap) {
    return false;
  }
  memcpy(dst, s, len);
  dst[len] = '\0';
  return true;
}

// 规则字段合法性（落盘数据消毒与解析共用同一判定）
static inline bool ykRuleValid(const YkRule* r) {
  return r != NULL && r->hour <= 23 && r->minute <= 59 && r->action <= 1 &&
         r->weekMask >= 1 && r->weekMask <= 127;
}

// 解析单条规则 "TIME,ACT,MASK"，成功填 out 并返回 true
static inline bool ykParseOneRule(const char* tok, size_t len, YkRule* out) {
  char tmp[32];
  if (!ykTrimCopy(tmp, sizeof(tmp), tok, len)) {
    return false;
  }
  if (tmp[0] == '\0') {
    return false;
  }
  char* c1 = strchr(tmp, ',');
  if (c1 == NULL) {
    return false;
  }
  char* c2 = strchr(c1 + 1, ',');
  if (c2 == NULL || strchr(c2 + 1, ',') != NULL) {
    return false;
  }
  // 时间：HHMM 或 HH:MM
  int hour, minute;
  char* colon = strchr(tmp, ':');
  if (colon != NULL && colon < c1) {
    int hh, mm;
    if (!ykParseUint(tmp, (size_t)(colon - tmp), &hh) ||
        !ykParseUint(colon + 1, (size_t)(c1 - colon - 1), &mm)) {
      return false;
    }
    hour = hh;
    minute = mm;
  } else {
    int v;
    if (!ykParseUint(tmp, (size_t)(c1 - tmp), &v) || v > 9999) {
      return false;
    }
    // 必须恰好 4 位 HHMM，避免 "730" 之类歧义写法悄悄通过
    if (c1 - tmp != 4) {
      return false;
    }
    hour = v / 100;
    minute = v % 100;
  }
  if (hour > 23 || minute > 59) {
    return false;
  }
  // 动作：单个字符 0/1（容忍周围空白已在整段 trim，但字段内再稳一手）
  char actBuf[4];
  if (!ykTrimCopy(actBuf, sizeof(actBuf), c1 + 1, (size_t)(c2 - c1 - 1))) {
    return false;
  }
  if (!((actBuf[0] == '0' || actBuf[0] == '1') && actBuf[1] == '\0')) {
    return false;
  }
  char maskBuf[8];
  if (!ykTrimCopy(maskBuf, sizeof(maskBuf), c2 + 1, strlen(c2 + 1))) {
    return false;
  }
  int mask;
  if (!ykParseUint(maskBuf, strlen(maskBuf), &mask) || mask < 1 || mask > 127) {
    return false;
  }
  out->enabled = 1;
  out->hour = (uint8_t)hour;
  out->minute = (uint8_t)minute;
  out->action = (uint8_t)(actBuf[0] - '0');
  out->weekMask = (uint8_t)mask;
  return true;
}

// 解析时刻表文本，成功返回规则数（0..maxRules），非法返回 -1
static inline int ykParseSchedule(const char* text, YkRule* out, int maxRules) {
  if (text == NULL || out == NULL || maxRules <= 0) {
    return -1;
  }
  for (int i = 0; i < maxRules; i++) {
    out[i].enabled = 0;
  }
  int count = 0;
  const char* p = text;
  while (true) {
    const char* semi = strchr(p, ';');
    size_t len = semi ? (size_t)(semi - p) : strlen(p);
    // 跳过纯空白 token（尾分号/尾换行）
    bool blank = true;
    for (size_t i = 0; i < len; i++) {
      if (!isspace((unsigned char)p[i])) {
        blank = false;
        break;
      }
    }
    if (!blank) {
      if (count >= maxRules) {
        return -1;
      }
      if (!ykParseOneRule(p, len, &out[count])) {
        return -1;
      }
      count++;
    }
    if (semi == NULL) {
      break;
    }
    p = semi + 1;
  }
  return count;
}

// 格式化为标准文本（仅启用的规则），返回长度，buf 不足返回 -1
static inline int ykFormatSchedule(const YkRule* rules, int n, char* buf, size_t bufSize) {
  if (buf == NULL || bufSize == 0 || rules == NULL || n < 0) {
    return -1;
  }
  buf[0] = '\0';
  size_t used = 0;
  bool first = true;
  for (int i = 0; i < n; i++) {
    if (!rules[i].enabled) {
      continue;
    }
    int w = snprintf(buf + used, bufSize - used, "%s%02d%02d,%d,%d",
                     first ? "" : ";", rules[i].hour, rules[i].minute,
                     rules[i].action, rules[i].weekMask);
    if (w < 0 || (size_t)w >= bufSize - used) {
      return -1;
    }
    used += (size_t)w;
    first = false;
  }
  return (int)used;
}

// 某分钟是否有规则到期：wday 为 localtime 的 tm_wday（0=周日）。
// 返回到期规则的动作（同分钟多条时最后一条生效），无到期返回 -1
static inline int ykScheduleDue(const YkRule* rules, int n, int wday, int hour, int minute) {
  if (rules == NULL || wday < 0 || wday > 6) {
    return -1;
  }
  int bit = (wday + 6) % 7;  // tm_wday 周日=0 → bit6；周一=1 → bit0
  int result = -1;
  for (int i = 0; i < n; i++) {
    if (!rules[i].enabled) {
      continue;
    }
    if (rules[i].hour == hour && rules[i].minute == minute &&
        ((rules[i].weekMask >> bit) & 1)) {
      result = rules[i].action;
    }
  }
  return result;
}

// 断电/未对时期间错过的规则补执行：找出"当前时刻之前（含当前分钟）最近一次到期"的规则。
// 回看 7 天（一周内每条规则至少到期一次，只要掩码非空）。
// 入参为本地时间（tm_wday 0=周日）。找到返回动作（0/1）并写 *minutesAgo；无启用规则返回 -1。
// 同一时刻多条时与 ykScheduleDue 一致：数组中靠后的一条生效。
static inline int ykScheduleLastDue(const YkRule* rules, int n, int wday, int hour, int minute,
                                    long* minutesAgo) {
  if (rules == NULL || wday < 0 || wday > 6 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return -1;
  }
  const long nowMin = (long)hour * 60 + minute;
  long best = -1;
  int result = -1;
  for (int i = 0; i < n; i++) {
    if (!rules[i].enabled) {
      continue;
    }
    const long ruleMin = (long)rules[i].hour * 60 + rules[i].minute;
    for (int back = 0; back <= 7; back++) {
      long ago = (long)back * 1440 + (nowMin - ruleMin);
      if (ago < 0) {
        continue;  // 今天还没到
      }
      int d = ((wday - back) % 7 + 7) % 7;
      int bit = (d + 6) % 7;
      if ((rules[i].weekMask >> bit) & 1) {
        if (best < 0 || ago <= best) {
          best = ago;
          result = rules[i].action;
        }
        break;  // 该规则最近一次已找到
      }
    }
  }
  if (result >= 0 && minutesAgo != NULL) {
    *minutesAgo = best;
  }
  return result;
}

// 补执行用的「最后变化时刻」消毒：晚于现在 = 坏数据（旧固件没写过时间戳时
// EEPROM 读回 0xFF 擦除态、或时钟曾被拨快），按未知(0)处理；否则坏值会让
// scheduleCatchup 的比较恒成立、补执行被永久禁用。
static inline uint32_t ykSanitizeChangeMin(uint32_t changeMin, uint32_t nowMin) {
  return changeMin > nowMin ? 0 : changeMin;
}
