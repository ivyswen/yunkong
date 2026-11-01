// 主机侧测试：g++ -std=c++11 test_schedule.cpp -o test && ./test
#include "../12F/schedule.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main() {
  YkRule r[4];
  // 基本解析
  CHECK(ykParseSchedule("1730,0,127", r, 4) == 1);
  CHECK(r[0].enabled == 1 && r[0].hour == 17 && r[0].minute == 30 && r[0].action == 0 && r[0].weekMask == 127);
  CHECK(ykParseSchedule("07:30,1,31", r, 4) == 1);
  CHECK(r[0].hour == 7 && r[0].minute == 30 && r[0].action == 1 && r[0].weekMask == 31);
  CHECK(ykParseSchedule("0730,1,127;1730,0,127", r, 4) == 2);
  CHECK(ykParseSchedule("0730, 1, 127 ; 1730, 0, 96", r, 4) == 2);  // 容忍空格
  CHECK(ykParseSchedule("1730,0,127\n", r, 4) == 1);                  // 容忍尾换行（面板发送常见）
  CHECK(ykParseSchedule("1730,0,127;", r, 4) == 1);                   // 容忍尾分号
  // 清空合法
  CHECK(ykParseSchedule("", r, 4) == 0);
  CHECK(ykParseSchedule("  ", r, 4) == 0);
  // 非法整体拒绝
  CHECK(ykParseSchedule("2400,1,127", r, 4) == -1);
  CHECK(ykParseSchedule("1760,1,127", r, 4) == -1);
  CHECK(ykParseSchedule("1730,2,127", r, 4) == -1);
  CHECK(ykParseSchedule("1730,1,0", r, 4) == -1);
  CHECK(ykParseSchedule("1730,1,128", r, 4) == -1);
  CHECK(ykParseSchedule("1730,1", r, 4) == -1);
  CHECK(ykParseSchedule("1730", r, 4) == -1);
  CHECK(ykParseSchedule("abc", r, 4) == -1);
  CHECK(ykParseSchedule("0730,1,127;xx", r, 4) == -1);
  CHECK(ykParseSchedule("0100,1,127;0200,1,127;0300,1,127;0400,1,127;0500,1,127", r, 4) == -1);  // 超上限

  // 格式化（标准形）与回环
  char buf[128];
  CHECK(ykParseSchedule("07:30,1,31;1730,0,127", r, 4) == 2);
  CHECK(ykFormatSchedule(r, 2, buf, sizeof(buf)) > 0);
  CHECK(strcmp(buf, "0730,1,31;1730,0,127") == 0);
  CHECK(ykFormatSchedule(r, 0, buf, sizeof(buf)) == 0);
  CHECK(strcmp(buf, "") == 0);
  char tiny[6];
  CHECK(ykFormatSchedule(r, 2, tiny, sizeof(tiny)) == -1);

  // 触发判定：tm_wday 0=周日 1=周一 … 6=周六
  CHECK(ykParseSchedule("1730,0,127;0730,1,31;0800,1,64", r, 4) == 3);
  CHECK(ykScheduleDue(r, 3, 3, 17, 30) == 0);    // 周三 17:30 关（每天）
  CHECK(ykScheduleDue(r, 3, 0, 17, 30) == 0);    // 周日 17:30 关（每天）
  CHECK(ykScheduleDue(r, 3, 3, 17, 31) == -1);   // 过一分钟不再触发
  CHECK(ykScheduleDue(r, 3, 1, 7, 30) == 1);     // 周一 07:30 开（工作日）
  CHECK(ykScheduleDue(r, 3, 6, 7, 30) == -1);    // 周六不在工作日掩码
  CHECK(ykScheduleDue(r, 3, 0, 8, 0) == 1);      // 周日 08:00 开（掩码64=周日）
  CHECK(ykScheduleDue(r, 3, 6, 8, 0) == -1);     // 周六不匹配周日掩码
  // 同分钟两条，后者生效
  CHECK(ykParseSchedule("0900,1,127;0900,0,127", r, 4) == 2);
  CHECK(ykScheduleDue(r, 2, 2, 9, 0) == 0);
  // 禁用槽不触发
  r[0].enabled = 0;
  CHECK(ykScheduleDue(r, 2, 2, 9, 0) == 0);      // 第二条仍生效
  r[1].enabled = 0;
  CHECK(ykScheduleDue(r, 2, 2, 9, 0) == -1);

  // 规则合法性判定（落盘消毒共用）
  YkRule okRule = {1, 23, 59, 1, 127};
  CHECK(ykRuleValid(&okRule));
  YkRule badHour = {1, 24, 0, 1, 127};
  CHECK(!ykRuleValid(&badHour));
  YkRule badMask = {1, 12, 0, 0, 0};
  CHECK(!ykRuleValid(&badMask));
  CHECK(!ykRuleValid(NULL));

  // 错过规则补执行：最近一次到期
  long ago = -1;
  CHECK(ykParseSchedule("0730,1,127;1730,0,127", r, 4) == 2);
  CHECK(ykScheduleLastDue(r, 2, 3, 7, 40, &ago) == 1 && ago == 10);     // 周三 07:40 → 10 分钟前开
  CHECK(ykScheduleLastDue(r, 2, 3, 18, 0, &ago) == 0 && ago == 30);     // 周三 18:00 → 30 分钟前关
  CHECK(ykScheduleLastDue(r, 2, 3, 6, 0, &ago) == 0 && ago == 750);     // 周三 06:00 → 昨天 17:30 关
  CHECK(ykScheduleLastDue(r, 2, 3, 7, 30, &ago) == 1 && ago == 0);      // 当前分钟算到期
  // 工作日掩码：周一 06:00 往回找 → 上周五 17:30
  CHECK(ykParseSchedule("0730,1,31;1730,0,31", r, 4) == 2);
  CHECK(ykScheduleLastDue(r, 2, 1, 6, 0, &ago) == 0 && ago == (2 * 1440 + 750));
  // 仅周日 08:00，当前周日 07:00 → 上周日（7 天前）
  CHECK(ykParseSchedule("0800,1,64", r, 4) == 1);
  CHECK(ykScheduleLastDue(r, 1, 0, 7, 0, &ago) == 1 && ago == (7 * 1440 - 60));
  // 空表
  CHECK(ykParseSchedule("", r, 4) == 0);
  CHECK(ykScheduleLastDue(r, 4, 3, 12, 0, &ago) == -1);
  // 同一时刻两条：后者生效（与 ykScheduleDue 一致）
  CHECK(ykParseSchedule("0900,1,127;0900,0,127", r, 4) == 2);
  CHECK(ykScheduleLastDue(r, 2, 2, 10, 0, &ago) == 0 && ago == 60);
  CHECK(ykScheduleLastDue(r, 2, 7, 10, 0, &ago) == -1);  // 非法 wday

  if (fails == 0) printf("ALL PASS\n");
  return fails ? 1 : 0;
}
