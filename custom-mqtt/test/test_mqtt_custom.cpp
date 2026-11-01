// 主机侧测试：g++ -std=c++11 -I ../12F test_mqtt_custom.cpp -o test && ./test
#include "../12F/mqtt_custom.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static int parse(const char* s) { return ykParseSwitchPayload((const uint8_t*)s, strlen(s)); }

int main() {
  // 文本载荷
  CHECK(parse("ON") == 1);
  CHECK(parse("OFF") == 0);
  CHECK(parse("on") == 1);
  CHECK(parse("off") == 0);
  CHECK(parse("1") == 1);
  CHECK(parse("0") == 0);
  CHECK(parse(" ON ") == 1);      // 容忍首尾空白
  CHECK(parse("OFF\n") == 0);     // 容忍结尾换行
  // 单字节载荷（字节型面板）
  uint8_t b1 = 0x01, b0 = 0x00;
  CHECK(ykParseSwitchPayload(&b1, 1) == 1);
  CHECK(ykParseSwitchPayload(&b0, 1) == 0);
  // 无法识别的一律忽略
  CHECK(parse("") == -1);
  CHECK(parse("toggle") == -1);
  CHECK(parse("ONN") == -1);
  CHECK(parse("2") == -1);
  CHECK(ykParseSwitchPayload(NULL, 0) == -1);

  // topic 构造
  char buf[96];
  CHECK(ykBuildTopic(buf, sizeof(buf), "socket1", "set") > 0);
  CHECK(strcmp(buf, "yunkong/socket1/set") == 0);
  CHECK(ykBuildTopic(buf, sizeof(buf), "socket1", "state") > 0);
  CHECK(strcmp(buf, "yunkong/socket1/state") == 0);
  CHECK(ykBuildTopic(buf, sizeof(buf), "socket1", "online") > 0);
  CHECK(strcmp(buf, "yunkong/socket1/online") == 0);
  char tiny[8];
  CHECK(ykBuildTopic(tiny, sizeof(tiny), "socket1", "state") == -1);  // 缓冲不足明确失败

  if (fails == 0) printf("ALL PASS\n");
  return fails ? 1 : 0;
}
