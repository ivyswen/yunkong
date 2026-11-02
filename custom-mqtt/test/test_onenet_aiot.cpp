#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../12F/onenet_aiot.h"

void test_topics() {
  char buf[128];
  assert(onenetBuildSetTopic(buf, sizeof(buf), "MiXfbNnY6r", "dev1") > 0);
  assert(strcmp(buf, "$sys/MiXfbNnY6r/dev1/thing/property/set") == 0);

  assert(onenetBuildSetReplyTopic(buf, sizeof(buf), "MiXfbNnY6r", "dev1") > 0);
  assert(strcmp(buf, "$sys/MiXfbNnY6r/dev1/thing/property/set_reply") == 0);

  assert(onenetBuildPostTopic(buf, sizeof(buf), "MiXfbNnY6r", "dev1") > 0);
  assert(strcmp(buf, "$sys/MiXfbNnY6r/dev1/thing/property/post") == 0);

  // Buffer too small
  char small[10];
  assert(onenetBuildSetTopic(small, sizeof(small), "MiXfbNnY6r", "dev1") == -1);
}

void test_parse_set() {
  char id[32];

  // Case 1: standard boolean false
  const char* p1 = "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"switch1\":false}}";
  assert(onenetParseSetPayload((const uint8_t*)p1, strlen(p1), id, sizeof(id)) == 0);
  assert(strcmp(id, "1") == 0);

  // Case 2: standard boolean true
  const char* p2 = "{\"id\":\"1002\",\"version\":\"1.0\",\"params\":{\"switch1\":true}}";
  assert(onenetParseSetPayload((const uint8_t*)p2, strlen(p2), id, sizeof(id)) == 1);
  assert(strcmp(id, "1002") == 0);

  // Case 3: total_switch with integer 1
  const char* p3 = "{\"id\":\"abc_123\",\"params\":{\"total_switch\": 1}}";
  assert(onenetParseSetPayload((const uint8_t*)p3, strlen(p3), id, sizeof(id)) == 1);
  assert(strcmp(id, "abc_123") == 0);

  // Case 4: total_switch with string "off"
  const char* p4 = "{\"id\":\"77\",\"params\":{\"total_switch\": \"off\"}}";
  assert(onenetParseSetPayload((const uint8_t*)p4, strlen(p4), id, sizeof(id)) == 0);
  assert(strcmp(id, "77") == 0);

  // Case 5: numeric id without quotes
  const char* p5 = "{\"id\":999,\"params\":{\"switch1\":true}}";
  assert(onenetParseSetPayload((const uint8_t*)p5, strlen(p5), id, sizeof(id)) == 1);
  assert(strcmp(id, "999") == 0);

  // Case 6: unrelated property
  const char* p6 = "{\"id\":\"88\",\"params\":{\"Voltage\": 220}}";
  assert(onenetParseSetPayload((const uint8_t*)p6, strlen(p6), id, sizeof(id)) == -1);
}

void test_build_payloads() {
  char buf[256];
  assert(onenetBuildSetReply(buf, sizeof(buf), "1001", 200, "success") > 0);
  assert(strcmp(buf, "{\"id\":\"1001\",\"code\":200,\"msg\":\"success\"}") == 0);

  assert(onenetBuildPropertyPost(buf, sizeof(buf), "2001", 1) > 0);
  assert(strcmp(buf, "{\"id\":\"2001\",\"version\":\"1.0\",\"params\":{\"switch1\":{\"value\":true},\"total_switch\":{\"value\":true}}}") == 0);

  assert(onenetBuildPropertyPost(buf, sizeof(buf), "2002", 0) > 0);
  assert(strcmp(buf, "{\"id\":\"2002\",\"version\":\"1.0\",\"params\":{\"switch1\":{\"value\":false},\"total_switch\":{\"value\":false}}}") == 0);
}

int main() {
  test_topics();
  test_parse_set();
  test_build_payloads();
  printf("ALL ONENET AIOT TESTS PASS\n");
  return 0;
}
