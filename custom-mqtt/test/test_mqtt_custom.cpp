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
  CHECK(parse("ON garbage") == -1);  // 正文后跟非空白内容不认
  CHECK(parse("OFF x") == -1);
  CHECK(parse("O N") == -1);
  CHECK(parse("ON \r\n") == 1);    // 结尾多种空白仍认

  // 设备 ID 校验
  CHECK(ykDeviceIdValid("socket1"));
  CHECK(ykDeviceIdValid("ykyc-1a2b3c"));
  CHECK(ykDeviceIdValid("Living_Room-2"));
  CHECK(!ykDeviceIdValid(""));
  CHECK(!ykDeviceIdValid(NULL));
  CHECK(!ykDeviceIdValid("a/b"));
  CHECK(!ykDeviceIdValid("+"));
  CHECK(!ykDeviceIdValid("#"));
  CHECK(!ykDeviceIdValid("a\"b"));
  CHECK(!ykDeviceIdValid("<img>"));
  CHECK(!ykDeviceIdValid("has space"));
  CHECK(!ykDeviceIdValid("123456789012345678901234567890123"));  // 33 位超长
  CHECK(ykDeviceIdValid("12345678901234567890123456789012"));    // 32 位上限

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

  // 订阅后忽略窗口（防 retained set 重投抵消时刻表/按键动作）：窗口内只清不执行
  CHECK(ykWithinIgnoreWindow(1000, 3500));        // 窗口内 -> 忽略执行
  CHECK(!ykWithinIgnoreWindow(3500, 3500));       // 到界 -> 放行（与 OneNET 分支 >=0 放行一致）
  CHECK(!ykWithinIgnoreWindow(4000, 3500));       // 窗口外 -> 放行
  CHECK(!ykWithinIgnoreWindow(12345, 0));         // 未布防（初始 0）-> 放行
  CHECK(ykWithinIgnoreWindow(4294967195UL, 100)); // millis 环绕：until 在回绕后仍未到 -> 忽略
  CHECK(!ykWithinIgnoreWindow(200, 100));         // 环绕后已过界 -> 放行

  // 模式判定：服务器非空且非 OneNET 云域名 -> 自定义模式（与密码/端口无关）
  CHECK(ykIsCustomBrokerHost("192.168.1.10"));
  CHECK(ykIsCustomBrokerHost("broker.local"));
  CHECK(ykIsCustomBrokerHost("10.0.0.5"));
  CHECK(!ykIsCustomBrokerHost(""));
  CHECK(!ykIsCustomBrokerHost(NULL));
  CHECK(!ykIsCustomBrokerHost("MiXfbNnY6r.mqttstls.acc.cmcconenet.cn"));
  CHECK(!ykIsCustomBrokerHost("mqttstls.heclouds.com"));

  if (fails == 0) printf("ALL PASS\n");
  return fails ? 1 : 0;
}
