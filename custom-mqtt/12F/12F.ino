#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
// PubSubClient: 缓冲区由 setup() 里的 mqttclient.setBufferSize(512) 设定
#include <PubSubClient.h>

#define eeshow
#define eeshowM 2
#define useLittleFS
#include <BGWiFiConfig.h>
#include "mqtt_custom.h"
#include "schedule.h"
#include "onenet_aiot.h"

// OneNET 官方公开根证书 (有效期至 2049-05-21)
static const char mqtt_ca_crt[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDOzCCAiOgAwIBAgIJAPCCNfxANtVEMA0GCSqGSIb3DQEBCwUAMDQxCzAJBgNV
BAYTAkNOMQ4wDAYDVQQKDAVDTUlPVDEVMBMGA1UEAwwMT25lTkVUIE1RVFRTMB4X
DTE5MDUyOTAxMDkyOFoXDTQ5MDUyMTAxMDkyOFowNDELMAkGA1UEBhMCQ04xDjAM
BgNVBAoMBUNNSU9UMRUwEwYDVQQDDAxPbmVORVQgTVFUVFMwggEiMA0GCSqGSIb3
DQEBAQUAA4IBDwAwggEKAoIBAQC/VvJ6lGWfy9PKdXKBdzY83OERB35AJhu+9jkx
5d4SOtZScTe93Xw9TSVRKrFwu5muGgPusyAlbQnFlZoTJBZY/745MG6aeli6plpR
r93G6qVN5VLoXAkvqKslLZlj6wXy70/e0GC0oMFzqSP0AY74icANk8dUFB2Q8usS
UseRafNBcYfqACzF/Wa+Fu/upBGwtl7wDLYZdCm3KNjZZZstvVB5DWGnqNX9HkTl
U9NBMS/7yph3XYU3mJqUZxryb8pHLVHazarNRppx1aoNroi+5/t3Fx/gEa6a5PoP
ouH35DbykmzvVE67GUGpAfZZtEFE1e0E/6IB84PE00llvy3pAgMBAAGjUDBOMB0G
A1UdDgQWBBTTi/q1F2iabqlS7yEoX1rbOsz5GDAfBgNVHSMEGDAWgBTTi/q1F2ia
bqlS7yEoX1rbOsz5GDAMBgNVHRMEBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQAL
aqJ2FgcKLBBHJ8VeNSuGV2cxVYH1JIaHnzL6SlE5q7MYVg+Ofbs2PRlTiWGMazC7
q5RKVj9zj0z/8i3ScWrWXFmyp85ZHfuo/DeK6HcbEXJEOfPDvyMPuhVBTzuBIRJb
41M27NdIVCdxP6562n6Vp0gbE8kN10q+ksw8YBoLFP0D1da7D5WnSV+nwEIP+F4a
3ZX80bNt6tRj9XY0gM68mI60WXrF/qYL+NUz+D3Lw9bgDSXxpSN8JGYBR85BxBvR
NNAhsJJ3yoAvbPUQ4m8J/CoVKKgcWymS1pvEHmF47pgzbbjm5bdthlIx+swdiGFa
WzdhzTYwVkxBaU+xf/2w
-----END CERTIFICATE-----
)EOF";

// MQTT 心跳包间隔 (秒)
#define KEEPALIVE 15

//***********************主要引脚
//蓝灯 GPIO12
#define LED_B 12
//红灯 GPIO16
#define LED_RED 16
//继电器 GPIO14
#define FLAG 14
//按钮 GPIO13
#define BUTTON 13

BGWiFiConfig wifipw;

String html = "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\"><title>云控智能插座配网</title>"
              "<style> .input { border: 2px solid orange; padding: 10px; border-radius: 50px 20px; } .button { display: inline-block; padding: 15px 25px; font-size: 24px; cursor: pointer; text-align: center; text-decoration: none; outline: none; color: #fff; background-color: #4CAF50; border: none; border-radius: 15px; box-shadow: 0 9px #999; } .button:hover { background-color: #3e8e41 } .button:active { background-color: #3e8e41; box-shadow: 0 5px #666; transform: translateY(4px); } </style>"
              "</head><body><center><form action=\"htmlresult\" method=\"post\">"
              "<h4>WiFi名称:<input class=\"input\" type=\"text\" name=\"ssid\"></h4>"
              "<h4>WiFi密码:<input class=\"input\" type=\"text\" name=\"pwd\"></h4>"
              "<h4>设备名称(OneNET设备名):<input class=\"input\" type=\"text\" name=\"umsg1\"></h4>"
              "<h4>OneNET Token(连接密码):<input class=\"input\" type=\"text\" name=\"umsg2\"></h4>"
              "<h4>服务器(留空=OneNET MQTTS TLS):<input class=\"input\" type=\"text\" name=\"umsg3\"></h4>"
              "<h4>端口(OneNET留空=8883):<input class=\"input\" type=\"text\" name=\"umsg4\"></h4>"
              "<h4>产品ID/用户名(PID):<input class=\"input\" type=\"text\" name=\"umsg5\"></h4>"
              "<h4>密码(自建MQTT留空填此):<input class=\"input\" type=\"text\" name=\"umsg6\"></h4>"
              "<br>模式选择：<input type=\"radio\" name=\"mode\" value=\"1\" checked>mode:1<br>"
              "<input type=\"submit\" value=\"开始配网\"></form></body></html>";

PubSubClient mqttclient;
WiFiClient plainMqttClient;
BearSSL::WiFiClientSecure tlsWifiClient;
BearSSL::X509List mqttcert(mqtt_ca_crt);

/** 断电保持继电器逻辑状态；偏移避开 EEPROM 低地址，减少与库/SDK 冲突 */
static const uint8_t kPersistMagic = 0xA7;
static const int kPersistEepromOffset = 64;
static const int kPersistEepromSize = 192;

// 配网参数（全部存 EEPROM，不含硬编码敏感信息）
String CLIENTID;     // umsg1: 设备名
String SIGN_MQTT;    // umsg2: OneNET 鉴权 Token
String MQTT_HOST;    // umsg3: 服务器域名/IP (留空自动生成 OneNET 域名)
int MQTT_PORT_N = 8883; // umsg4: 端口 (OneNET 默认 8883)
String MQTT_USER;    // umsg5: 产品 ID (PID)
String MQTT_PASS;    // umsg6: 自建 MQTT 密码
bool customMqtt = false;
int lastReportedFlag = -1;

// 定点时刻表状态登记：规则与时区存 EEPROM 偏移 128 起（magic 0x5C）
static const int kSchedEepromOffset = 128;
static const uint8_t kSchedMagic = 0x5C;
static const int SCHED_MAX_RULES = 4;
YkRule schedRules[SCHED_MAX_RULES];
int8_t tzOffsetHours = 8;
bool schedClockInit = false;
unsigned long lastSchedNtpTry = 0;
long lastSchedMinute = -1;

// 按钮 按下时间
int buttonState = LOW;
unsigned long buttonMillis;

// 开关状态 延时开关时间
int flagState = HIGH; 
unsigned long timeOut = 0; 
bool restoreArmed = false;
unsigned long restoreAtMillis = 0;
int restoreTargetState = HIGH;

// MQTT消息更新间隔   客户端状态
unsigned long updateTime = 0; 
int mqttclient_state = 0;
unsigned long mqttReconnectDelayMs = 2000;
const unsigned long MQTT_RECONNECT_MIN_MS = 2000;
const unsigned long MQTT_RECONNECT_MAX_MS = 60000;
bool clockReady = false;
static const time_t kClockValidEpoch = 1700000000;
static unsigned long mqttIgnoreRelayDownlinkUntil = 0;
static unsigned long customSetIgnoreUntil = 0;

// LED
unsigned long ledTime = 0;
bool bLedState = LOW;
bool rLedState = HIGH;

// WiFi 离线恢复：STA 断连 2 分钟 → AP，AP 2 分钟 → 切回 STA 重试
unsigned long wifiLostAt = 0;
bool recoveryApActive = false;
unsigned long recoveryApEnteredAt = 0;
const unsigned long RECOVERY_ENTER_MS = 120000;
const unsigned long RECOVERY_AP_MS = 120000;

// 继电器最后一次变化的时刻（Unix 分钟）
uint32_t lastFlagChangeMin = 0;
bool schedCatchupDone = false;

static uint32_t nowEpochMinutesOrZero() {
  time_t now = time(nullptr);
  return now >= kClockValidEpoch ? (uint32_t)(now / 60) : 0;
}

// NTP 对时函数，TLS X.509 证书校验需要有效系统时间
void setClock()
{
  if (clockReady) {
    return;
  }
  configTime(tzOffsetHours * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  unsigned long started = millis();
  while (now < kClockValidEpoch)
  {
    if (millis() - started > 15000UL) {
      Serial.println(" timeout");
      return;
    }
    delay(400);
    yield();
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
  clockReady = true;
}

void loadPersistedFlagState() {
  EEPROM.begin(kPersistEepromSize);
  int off = kPersistEepromOffset;
  uint8_t magic = EEPROM.read(off);
  uint8_t v = EEPROM.read(off + 1);
  if (magic != kPersistMagic || v > 1u) {
    magic = EEPROM.read(0);
    v = EEPROM.read(1);
    if (magic == kPersistMagic && v <= 1u) {
      flagState = v ? HIGH : LOW;
      EEPROM.write(off, kPersistMagic);
      EEPROM.write(off + 1, v);
      EEPROM.write(0, 0);
      EEPROM.write(1, 0);
      EEPROM.commit();
    }
    EEPROM.end();
    return;
  }
  flagState = v ? HIGH : LOW;
  lastFlagChangeMin = (uint32_t)EEPROM.read(off + 2) | ((uint32_t)EEPROM.read(off + 3) << 8) |
                      ((uint32_t)EEPROM.read(off + 4) << 16) | ((uint32_t)EEPROM.read(off + 5) << 24);
  EEPROM.end();
}

void persistFlagState() {
  uint8_t bit = (uint8_t)(flagState & 1);
  int off = kPersistEepromOffset;
  EEPROM.begin(kPersistEepromSize);
  if (EEPROM.read(off) != kPersistMagic || EEPROM.read(off + 1) != bit) {
    lastFlagChangeMin = nowEpochMinutesOrZero();
    EEPROM.write(off, kPersistMagic);
    EEPROM.write(off + 1, bit);
    for (int i = 0; i < 4; i++) {
      EEPROM.write(off + 2 + i, (uint8_t)(lastFlagChangeMin >> (8 * i)));
    }
    EEPROM.commit();
  }
  EEPROM.end();
}

void clearPersistedFlagState() {
  int off = kPersistEepromOffset;
  EEPROM.begin(kPersistEepromSize);
  EEPROM.write(off, 0);
  EEPROM.write(off + 1, 0);
  for (int i = 0; i < 4; i++) {
    EEPROM.write(off + 2 + i, 0);
  }
  lastFlagChangeMin = 0;
  EEPROM.write(0, 0);
  EEPROM.write(1, 0);
  EEPROM.commit();
  EEPROM.end();
}

// 时刻表 EEPROM 存取
void loadSchedule() {
  for (int i = 0; i < SCHED_MAX_RULES; i++) {
    schedRules[i].enabled = 0;
  }
  tzOffsetHours = 8;
  EEPROM.begin(kPersistEepromSize);
  uint8_t magic = EEPROM.read(kSchedEepromOffset);
  if (magic != kSchedMagic) {
    EEPROM.end();
    return;
  }
  int8_t tz = (int8_t)EEPROM.read(kSchedEepromOffset + 1);
  if (tz >= -12 && tz <= 14) {
    tzOffsetHours = tz;
  }
  for (int i = 0; i < SCHED_MAX_RULES; i++) {
    int base = kSchedEepromOffset + 2 + i * 5;
    YkRule r;
    r.enabled = EEPROM.read(base) ? 1 : 0;
    r.hour = EEPROM.read(base + 1);
    r.minute = EEPROM.read(base + 2);
    r.action = EEPROM.read(base + 3);
    r.weekMask = EEPROM.read(base + 4);
    if (r.enabled && ykRuleValid(&r)) {
      schedRules[i] = r;
    } else {
      schedRules[i].enabled = 0;
    }
  }
  EEPROM.end();
}

static inline bool eepromPut(int addr, uint8_t val) {
  if (EEPROM.read(addr) != val) {
    EEPROM.write(addr, val);
    return true;
  }
  return false;
}

void saveSchedule() {
  EEPROM.begin(kPersistEepromSize);
  bool dirty = false;
  dirty |= eepromPut(kSchedEepromOffset, kSchedMagic);
  dirty |= eepromPut(kSchedEepromOffset + 1, (uint8_t)tzOffsetHours);
  for (int i = 0; i < SCHED_MAX_RULES; i++) {
    int base = kSchedEepromOffset + 2 + i * 5;
    const YkRule& r = schedRules[i];
    uint8_t en = (r.enabled && ykRuleValid(&r)) ? 1 : 0;
    dirty |= eepromPut(base, en);
    dirty |= eepromPut(base + 1, r.hour);
    dirty |= eepromPut(base + 2, r.minute);
    dirty |= eepromPut(base + 3, r.action);
    dirty |= eepromPut(base + 4, r.weekMask);
  }
  if (dirty) {
    EEPROM.commit();
  }
  EEPROM.end();
}

void clearScheduleStorage() {
  EEPROM.begin(kPersistEepromSize);
  EEPROM.write(kSchedEepromOffset, 0);
  EEPROM.commit();
  EEPROM.end();
  for (int i = 0; i < SCHED_MAX_RULES; i++) {
    schedRules[i].enabled = 0;
  }
}

int currentScheduleText(char* buf, size_t bufSize) {
  return ykFormatSchedule(schedRules, SCHED_MAX_RULES, buf, bufSize);
}

void schedApplyTimezone() {
  configTime(tzOffsetHours * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
  schedClockInit = true;
  lastSchedNtpTry = millis();
}

bool schedEnsureClock() {
  if (!schedClockInit) {
    schedApplyTimezone();
  }
  if (time(nullptr) >= kClockValidEpoch) {
    return true;
  }
  if (!schedClockInit || millis() - lastSchedNtpTry > 60000UL) {
    schedApplyTimezone();
  }
  return false;
}

void updateFlagStart();

void scheduleCatchup(time_t now, const struct tm& ti) {
  long minutesAgo = -1;
  int lastAction = ykScheduleLastDue(schedRules, SCHED_MAX_RULES, ti.tm_wday, ti.tm_hour, ti.tm_min, &minutesAgo);
  if (lastAction < 0 || minutesAgo < 0) {
    return;
  }
  uint32_t nowMin = (uint32_t)(now / 60);
  uint32_t ruleEpochMin = nowMin >= (uint32_t)minutesAgo ? nowMin - (uint32_t)minutesAgo : 0;
  if (lastFlagChangeMin > 0 && lastFlagChangeMin >= ruleEpochMin) {
    return;
  }
  int targetState = lastAction ? HIGH : LOW;
  if (flagState != targetState) {
    Serial.printf("schedule catchup -> %s (rule was %ldm ago)\n", lastAction ? "ON" : "OFF", minutesAgo);
    flagState = targetState;
    clearLocalRestore("schedule_catchup");
    updateFlagStart();
    persistFlagState();
  }
}

void scheduleLoop() {
  if (!schedEnsureClock()) {
    return;
  }
  time_t now = time(nullptr);
  if (!clockReady) {
    clockReady = true;
    Serial.println("schedule clock ready");
  }
  long minuteStamp = (long)(now / 60);
  if (minuteStamp == lastSchedMinute) {
    return;
  }
  lastSchedMinute = minuteStamp;
  struct tm ti;
  localtime_r(&now, &ti);
  if (!schedCatchupDone) {
    schedCatchupDone = true;
    scheduleCatchup(now, ti);
  }
  int action = ykScheduleDue(schedRules, SCHED_MAX_RULES, ti.tm_wday, ti.tm_hour, ti.tm_min);
  if (action < 0) {
    return;
  }
  Serial.printf("schedule due -> %s\n", action ? "ON" : "OFF");
  flagState = action ? HIGH : LOW;
  clearLocalRestore("schedule_due");
  updateFlagStart();
  persistFlagState();
}

/** OTA 首页，须先于 OTAbegin */
static const char otaIndexHtml[] PROGMEM =
    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>云控智能插座 / 维护</title>"
    "<style>body{font-family:sans-serif;padding:16px;max-width:480px}"
    "hr{border:none;border-top:1px solid #ccc}"
    "#status{margin-top:24px;padding:12px;background:#f5f5f5;border-radius:8px;font-size:14px}"
    "#status .ok{color:#27ae60}#status .err{color:#e74c3c}"
    "#status td{padding:4px 8px}#status td:first-child{color:#666}"
    "input[type=text],input[type=number]{padding:6px;width:100%;box-sizing:border-box}"
    "button,input[type=submit]{cursor:pointer}"
    "</style></head><body>"
    "<h3>固件 OTA</h3>"
    "<form method=\"POST\" action=\"/otaupdate\" enctype=\"multipart/form-data\""
    "onsubmit=\"stopPolling();document.getElementById('status').innerHTML='升级中，请等待…'\">"
    "<input type=\"file\" name=\"otaupdate\"><br><br>"
    "<input type=\"submit\" value=\"开始升级\"></form>"
    "<hr><h3>定点时刻表</h3>"
    "<p style=\"font-size:12px;color:#666\">最多 4 条，格式：<code>0730,1,127;1730,0,127</code><br>"
    "掩码：127=每天，31=工作日，96=周末。空=清除全部。</p>"
    "<form method=\"POST\" action=\"/schedule\">"
    "<label>规则文本：<input type=\"text\" name=\"sched\" id=\"sched\"></label><br><br>"
    "<label>时区（UTC 偏移小时）：<input type=\"number\" name=\"tz\" id=\"tz\" min=\"-12\" max=\"14\" value=\"8\"></label><br><br>"
    "<input type=\"submit\" value=\"保存时刻表\"></form>"
    "<hr><h3>恢复配网</h3>"
    "<p>清除已保存的 WiFi 与参数，重启后进入热点配网。</p>"
    "<form method=\"POST\" action=\"/otaclearcfg\" "
    "onsubmit=\"return confirm('确定清除配网？将断网直至重新配网。');\">"
    "<button type=\"submit\" style=\"background:#c0392b;color:#fff;padding:10px 16px;border:none\""
    ">清除配网并重启</button></form>"
    "<hr><h3>实时状态</h3>"
    "<div id=\"status\">加载中…</div>"
    "<script>"
    "var pollTimer;"
    "function stopPolling(){if(pollTimer){clearTimeout(pollTimer);pollTimer=null;}}"
    "function ok(v){return '<span class=ok>'+v+' ✓</span>'}"
    "function err(v){return '<span class=err>'+v+' ✗</span>'}"
    "function esc(v){return String(v).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}"
    "function fetchStatus(){"
    "var x=new XMLHttpRequest();x.open('GET','/status');x.timeout=4000;"
    "x.onload=function(){"
    "try{var s=JSON.parse(x.responseText);"
    "var h='<table>';"
    "h+='<tr><td>WiFi</td><td>'+(s.wifi_status==3?ok(s.wifi_str):err(s.wifi_str))+'</td></tr>';"
    "h+='<tr><td>IP</td><td>'+esc(s.ip||'—')+'</td></tr>';"
    "h+='<tr><td>模式</td><td>'+esc(s.mqtt_mode)+'</td></tr>';"
    "h+='<tr><td>MQTT</td><td>'+(s.mqtt_ready?ok('OK'):err('NO'))+'</td></tr>';"
    "h+='<tr><td>NTP</td><td>'+(s.clock_ready?ok('OK'):err('NO'))+'</td></tr>';"
    "h+='<tr><td>继电器</td><td>'+(s.flag_state==1?'开（蓝灯）':'关（红灯）')+'</td></tr>';"
    "h+='<tr><td>设备ID</td><td style=font-size:11px>'+esc(s.clientid||'—')+'</td></tr>';"
    "h+='</table>';"
    "document.getElementById('status').innerHTML=h;"
    "var sc=document.getElementById('sched');if(sc&&document.activeElement!==sc)sc.value=s.schedule||'';"
    "var tzi=document.getElementById('tz');if(tzi&&document.activeElement!==tzi)tzi.value=(s.tz!==undefined?s.tz:8);"
    "}catch(e){document.getElementById('status').innerHTML='解析失败'}"
    "pollTimer=setTimeout(fetchStatus,2000);"
    "};"
    "x.onerror=function(){document.getElementById('status').innerHTML=err('请求失败');pollTimer=setTimeout(fetchStatus,2000);};"
    "x.ontimeout=function(){document.getElementById('status').innerHTML=err('超时');pollTimer=setTimeout(fetchStatus,2000);};"
    "x.send();"
    "}"
    "fetchStatus();"
    "</script>"
    "</body></html>";

static const char* wifiStatusStr(int st) {
  switch (st) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "?";
  }
}

void publishSchedule();

void handleOtaClearCfg() {
  OTAserver.sendHeader("Connection", "close");
  OTAserver.send(200, "text/html; charset=utf-8",
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>OK</title></head><body>"
    "<p>已清除，正在重启并进入配网…</p></body></html>");
  mqttclient.disconnect();
  clearPersistedFlagState();
  clearScheduleStorage();
  wifipw.clearWiFi();
  delay(400);
  ESP.restart();
}

void handleScheduleSave() {
  String sched = OTAserver.arg("sched");
  String tzStr = OTAserver.arg("tz");
  YkRule parsed[SCHED_MAX_RULES];
  int cnt = ykParseSchedule(sched.c_str(), parsed, SCHED_MAX_RULES);
  if (cnt < 0) {
    OTAserver.send(400, "text/html; charset=utf-8",
      "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>ERR</title></head><body>"
      "<p>时刻表格式错误，未修改。格式：0730,1,127;1730,0,127</p></body></html>");
    return;
  }
  memcpy(schedRules, parsed, sizeof(schedRules));
  if (tzStr.length() > 0) {
    int tz = tzStr.toInt();
    if (tz >= -12 && tz <= 14) {
      tzOffsetHours = (int8_t)tz;
    }
  }
  saveSchedule();
  schedApplyTimezone();
  lastSchedMinute = -1;
  if (customMqtt && mqttclient_state == 1) {
    publishSchedule();
  }
  OTAserver.send(200, "text/html; charset=utf-8",
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>OK</title></head><body>"
    "<p>时刻表已保存。</p></body></html>");
}

static String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((unsigned char)c < 0x20) {
      char tmp[8];
      snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)c);
      out += tmp;
    } else {
      out += c;
    }
  }
  return out;
}

void handleStatus() {
  String json = "{";
  json += "\"wifi_status\":" + String(WiFi.status());
  json += ",\"wifi_str\":\"" + String(wifiStatusStr(WiFi.status())) + "\"";
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"mqtt_ready\":" + String(mqttclient_state == 1 ? "true" : "false");
  json += ",\"clock_ready\":" + String(clockReady ? "true" : "false");
  json += ",\"flag_state\":" + String(flagState == HIGH ? "1" : "0");
  json += ",\"clientid\":\"" + jsonEscape(CLIENTID) + "\"";
  json += ",\"mqtt_mode\":\"" + String(customMqtt ? "custom_mqtt" : "onenet_tls") + "\"";
  char schedBuf[128];
  currentScheduleText(schedBuf, sizeof(schedBuf));
  json += ",\"schedule\":\"" + String(schedBuf) + "\"";
  json += ",\"tz\":" + String(tzOffsetHours);
  json += ",\"reconnect_delay_ms\":" + String(mqttReconnectDelayMs);
  json += "}";
  OTAserver.sendHeader("Access-Control-Allow-Origin", "*");
  OTAserver.send(200, "application/json", json);
}

void enterRecoveryAP() {
  WiFi.softAP("SmartSocket_Recovery", "");
  OTAserver.begin();
  recoveryApActive = true;
  recoveryApEnteredAt = millis();
  wifiLostAt = 0;
}

void exitRecoveryAP() {
  WiFi.softAPdisconnect(true);
  recoveryApActive = false;
}

void updateFlagStart()
{
  if(flagState == LOW){
    digitalWrite(FLAG, LOW);
  }else if(flagState == HIGH){
    digitalWrite(FLAG, HIGH);
  }
  Serial.print("updateFlagStart -> ");
  Serial.println(flagState);
}

void clearLocalRestore(const char* reason)
{
  restoreArmed = false;
  restoreAtMillis = 0;
  Serial.print("clearLocalRestore -> ");
  Serial.println(reason);
}

void armLocalRestore(int currentCmdState, unsigned long delaySec)
{
  if (currentCmdState < 0 || currentCmdState > 1 || delaySec == 0) {
    clearLocalRestore("invalid_or_zero_delay");
    return;
  }
  if (delaySec > 604800UL) {
    delaySec = 604800UL;
  }
  restoreTargetState = currentCmdState == HIGH ? LOW : HIGH;
  restoreAtMillis = millis() + delaySec * 1000UL;
  restoreArmed = true;
  Serial.print("armLocalRestore -> delaySec=");
  Serial.print(delaySec);
  Serial.print(", target=");
  Serial.println(restoreTargetState);
}

void localRestoreLoop()
{
  if (!restoreArmed) {
    return;
  }
  if ((long)(millis() - restoreAtMillis) < 0) {
    return;
  }
  restoreArmed = false;
  flagState = restoreTargetState;
  updateFlagStart();
  persistFlagState();
  Serial.println("localRestoreLoop -> restored");
}

void publishOneNetState();

void receivedCallback(char* topicname, uint8_t* payload, unsigned int length)
{
  if (customMqtt) {
    char tSet[96];
    if (ykBuildTopic(tSet, sizeof(tSet), CLIENTID.c_str(), "set") > 0 && strcmp(topicname, tSet) == 0) {
      int v = ykParseSwitchPayload(payload, length);
      Serial.print("custom mqtt set -> ");
      Serial.println(v);
      if (v >= 0 && ykWithinIgnoreWindow(millis(), customSetIgnoreUntil)) {
        Serial.println("custom mqtt set ignored: post-subscribe window");
      } else if (v >= 0) {
        flagState = v ? HIGH : LOW;
        clearLocalRestore("custom_mqtt_set");
        updateFlagStart();
        persistFlagState();
      }
      if (length > 0) {
        mqttclient.publish(tSet, (const uint8_t*)"", 0, true);
      }
      return;
    }
    char tSchedSet[112];
    if (ykBuildTopic(tSchedSet, sizeof(tSchedSet), CLIENTID.c_str(), "schedule/set") > 0 &&
        strcmp(topicname, tSchedSet) == 0) {
      if (length < 200) {
        char text[200];
        memcpy(text, payload, length);
        text[length] = '\0';
        YkRule parsed[SCHED_MAX_RULES];
        int cnt = ykParseSchedule(text, parsed, SCHED_MAX_RULES);
        if (cnt >= 0) {
          memcpy(schedRules, parsed, sizeof(schedRules));
          saveSchedule();
          Serial.println("schedule updated via mqtt");
        } else {
          Serial.println("schedule parse failed, keep old");
        }
      }
      publishSchedule();
      return;
    }
    return;
  }

  // --- OneNET AIoT (TLS) 模式处理 ---
  char setTopic[96];
  if (onenetBuildSetTopic(setTopic, sizeof(setTopic), MQTT_USER.c_str(), CLIENTID.c_str()) > 0 &&
      strcmp(topicname, setTopic) == 0) {
    char msgId[32];
    int v = onenetParseSetPayload(payload, length, msgId, sizeof(msgId));
    Serial.print("onenet property/set -> ");
    Serial.print(v);
    Serial.print(" (msgId=");
    Serial.print(msgId);
    Serial.println(")");
    if (v >= 0) {
      flagState = (v == 1 ? HIGH : LOW);
      clearLocalRestore("onenet_set");
      updateFlagStart();
      persistFlagState();

      // 回复响应报文 (thing/property/set_reply)
      char replyTopic[96];
      char replyBuf[128];
      if (onenetBuildSetReplyTopic(replyTopic, sizeof(replyTopic), MQTT_USER.c_str(), CLIENTID.c_str()) > 0 &&
          onenetBuildSetReply(replyBuf, sizeof(replyBuf), msgId, 200, "success") > 0) {
        mqttclient.publish(replyTopic, replyBuf);
      }
      // 上报更新后的状态
      publishOneNetState();
    }
    return;
  }
}

void mqttBackoff() {
  updateTime = millis() + mqttReconnectDelayMs;
  mqttReconnectDelayMs = mqttReconnectDelayMs * 2;
  if (mqttReconnectDelayMs > MQTT_RECONNECT_MAX_MS) {
    mqttReconnectDelayMs = MQTT_RECONNECT_MAX_MS;
  }
}

void publishSchedule() {
  char topic[96];
  char text[128];
  if (ykBuildTopic(topic, sizeof(topic), CLIENTID.c_str(), "schedule") > 0 &&
      currentScheduleText(text, sizeof(text)) >= 0) {
    mqttclient.publish(topic, text, true);
  }
}

void publishCustomState() {
  char topic[96];
  if (ykBuildTopic(topic, sizeof(topic), CLIENTID.c_str(), "state") > 0) {
    mqttclient.publish(topic, flagState == HIGH ? "ON" : "OFF", true);
    lastReportedFlag = flagState;
  }
}

// OneNET AIoT: 上报当前开关状态 (thing/property/post)
void publishOneNetState() {
  char topic[96];
  char payload[192];
  if (onenetBuildPostTopic(topic, sizeof(topic), MQTT_USER.c_str(), CLIENTID.c_str()) > 0 &&
      onenetBuildPropertyPost(payload, sizeof(payload), "1", flagState == HIGH ? 1 : 0) > 0) {
    mqttclient.publish(topic, payload);
    lastReportedFlag = flagState;
    Serial.print("onenet state reported -> ");
    Serial.println(flagState == HIGH ? "ON" : "OFF");
  }
}

void mqtt_update_custom() {
  if (!wifipw.OK(true)) {
    updateTime = millis() + 1000;
    return;
  }
  if (mqttclient_state == 1) {
    if (!mqttclient.loop()) {
      Serial.println("custom mqttclient.loop fail!");
      mqttclient_state = 0;
      updateTime = millis() + mqttReconnectDelayMs;
      return;
    }
    if (lastReportedFlag != flagState) {
      publishCustomState();
    }
    return;
  }
  char tSet[96];
  char tOnline[96];
  if (ykBuildTopic(tSet, sizeof(tSet), CLIENTID.c_str(), "set") < 0 ||
      ykBuildTopic(tOnline, sizeof(tOnline), CLIENTID.c_str(), "online") < 0) {
    Serial.println("custom mqtt: topic too long, check device ID");
    updateTime = millis() + 5000;
    return;
  }
  if (mqttclient.connected()) {
    mqttclient.disconnect();
  } else {
    plainMqttClient.stop();
  }
  delay(50);
  mqttclient.setClient(plainMqttClient);
  mqttclient.setServer(MQTT_HOST.c_str(), (uint16_t)MQTT_PORT_N);
  mqttclient.setKeepAlive(KEEPALIVE);
  mqttclient.setSocketTimeout(10);
  mqttclient.setCallback(receivedCallback);
  char clientId[80];
  snprintf(clientId, sizeof(clientId), "yunkong-%s", CLIENTID.c_str());
  bool ok;
  if (MQTT_USER.length() > 0) {
    ok = mqttclient.connect(clientId, MQTT_USER.c_str(), MQTT_PASS.c_str(), tOnline, 1, true, "offline");
  } else {
    ok = mqttclient.connect(clientId, tOnline, 1, true, "offline");
  }
  if (ok) {
    mqttclient_state = 1;
    mqttReconnectDelayMs = MQTT_RECONNECT_MIN_MS;
    mqttclient.subscribe(tSet, 1);
    customSetIgnoreUntil = millis() + 2500UL;
    char tSchedSet[112];
    if (ykBuildTopic(tSchedSet, sizeof(tSchedSet), CLIENTID.c_str(), "schedule/set") > 0) {
      mqttclient.subscribe(tSchedSet, 1);
    }
    mqttclient.publish(tOnline, "online", true);
    publishSchedule();
    lastReportedFlag = -1;
    publishCustomState();
    Serial.print("custom mqtt connected -> ");
    Serial.println(MQTT_HOST);
    updateTime = millis() + 500;
  } else {
    Serial.printf("custom mqtt connect failed, status code = %d\n", mqttclient.state());
    mqttBackoff();
  }
}

// OneNET AIoT 主循环：TLS 8883 (MQTTS) + X.509 根证书 + Token 鉴权 + OneJSON 物模型
void mqtt_update_onenet() {
  if (!wifipw.OK(true)) {
    updateTime = millis() + 1000;
    return;
  }
  if (mqttclient_state == 1) {
    if (!mqttclient.loop()) {
      Serial.println("onenet TLS mqttclient.loop fail!");
      mqttclient_state = 0;
      updateTime = millis() + mqttReconnectDelayMs;
      return;
    }
    if (lastReportedFlag != flagState) {
      publishOneNetState();
    }
    return;
  }

  // 检查必备参数
  if (SIGN_MQTT.length() == 0 || CLIENTID.length() == 0 || MQTT_USER.length() == 0) {
    Serial.println("OneNET AIoT: 未配置设备名、Token 或 产品ID，请通过配网页 192.168.22.1 配置。");
    updateTime = millis() + 5000;
    return;
  }

  if (mqttclient.connected()) {
    mqttclient.disconnect();
  } else {
    tlsWifiClient.stop();
  }
  delay(50);

  // TLS 连接需要有效时间以校验 X.509 证书有效期
  tlsWifiClient.setTrustAnchors(&mqttcert);
  setClock();
  if (!clockReady) {
    Serial.println("Waiting for NTP clock sync for TLS...");
    updateTime = millis() + 1000;
    return;
  }

  // 计算或使用服务器域名
  String host = MQTT_HOST;
  if (host.length() == 0) {
    host = MQTT_USER + ".mqttstls.acc.cmcconenet.cn";
  }

  IPAddress mqttIp;
  if (!WiFi.hostByName(host.c_str(), mqttIp)) {
    Serial.printf("DNS failed for %s\n", host.c_str());
    updateTime = millis() + 2000;
    return;
  }

  tlsWifiClient.setX509Time(time(nullptr));
  tlsWifiClient.setBufferSizes(8192, 512);

  mqttclient.setClient(tlsWifiClient);
  mqttclient.setServer(mqttIp, (uint16_t)MQTT_PORT_N);
  mqttclient.setKeepAlive(KEEPALIVE);
  mqttclient.setSocketTimeout(12);
  mqttclient.setCallback(receivedCallback);

  Serial.printf("Connecting to OneNET AIoT TLS: host=%s:%d, pid=%s, dev=%s\n", host.c_str(), MQTT_PORT_N, MQTT_USER.c_str(), CLIENTID.c_str());

  if (mqttclient.connect(CLIENTID.c_str(), MQTT_USER.c_str(), SIGN_MQTT.c_str())) {
    mqttclient_state = 1;
    mqttReconnectDelayMs = MQTT_RECONNECT_MIN_MS;
    char setTopic[96];
    if (onenetBuildSetTopic(setTopic, sizeof(setTopic), MQTT_USER.c_str(), CLIENTID.c_str()) > 0) {
      mqttclient.subscribe(setTopic, 1);
      Serial.print("Subscribed: ");
      Serial.println(setTopic);
    }
    mqttIgnoreRelayDownlinkUntil = millis() + 2500UL;
    lastReportedFlag = -1;
    publishOneNetState();
    Serial.println("OneNET AIoT (TLS 8883) connected!");
    updateTime = millis() + 500;
  } else {
    Serial.printf("OneNET TLS connect failed, status code = %d\n", mqttclient.state());
    mqttBackoff();
  }
}

void mqtt_update() {
  if (customMqtt) {
    mqtt_update_custom();
  } else {
    mqtt_update_onenet();
  }
}

void led(){
  if(flagState == HIGH){
    if(rLedState){
      rLedState = false;
      digitalWrite(LED_RED, LOW);
    }
    if(wifipw.OK(true)){
      if(!bLedState){
        bLedState = true;
        digitalWrite(LED_B, HIGH);
      }
    }else{
      bLedState = !bLedState;
      digitalWrite(LED_B, bLedState);
    }
  }else{
    if(bLedState){
      bLedState = false;
      digitalWrite(LED_B, LOW);
    }
    if(wifipw.OK(true)){
      if(!rLedState){
        rLedState = true;
        digitalWrite(LED_RED, HIGH);
      }
    }else{
      rLedState = !rLedState;
      digitalWrite(LED_RED, rLedState);
    }
  }
}

void button_hok(){
  int buttonStateTmp = digitalRead(BUTTON);
  if (buttonStateTmp == HIGH) {
    if(buttonState == HIGH){
      buttonState = LOW;
      unsigned long downTime = millis() - buttonMillis;
      if(downTime < 50){
      }
      else if(downTime < 3000)
      {
        Serial.println("buttonState HIGH < 3s: toggle relay");
        flagState = (flagState == HIGH) ? LOW : HIGH;
        clearLocalRestore("manual_button_toggle");
        updateFlagStart();
        persistFlagState();
      }
      else
      {
        Serial.println("buttonState HIGH > 3s: reset config");
        clearPersistedFlagState();
        clearScheduleStorage();
        wifipw.clearWiFi();
      }
    }
  } else {
    if(buttonState == LOW){
      buttonState = HIGH;
      buttonMillis = millis();
    }else{
      Serial.print("按下时间：");
      Serial.print(millis() - buttonMillis);
      Serial.println("ms");
    }
  }
}

static String cleanUMSG(String s) {
  s.trim();
  if (s == "NULL" || s.indexOf("[err:") >= 0) {
    return "";
  }
  return s;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  mqttclient.setBufferSize(512);

  pinMode(LED_B, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(FLAG, OUTPUT);
  pinMode(BUTTON, INPUT);
  loadPersistedFlagState();
  loadSchedule();
  updateFlagStart();
  digitalWrite(LED_RED, HIGH);
  delay(100);

  Serial.println("Startup YKYC-001");

  wifipw.setCompatibleMode(true);
  wifipw.setWiFiTimeOut(10);
  wifipw.useSpaceWiFi(true);
  wifipw.autoStart(true);
  wifipw.setZDYhtml(html);
  wifipw.setNumUMSG(6);
  wifipw.begin();
  wifipw.setOTATimeOut(30);
  wifipw.setOTAhtml(String(FPSTR(otaIndexHtml)));
  wifipw.OTAbegin();
  OTAserver.on("/otaclearcfg", HTTP_POST, handleOtaClearCfg);
  OTAserver.on("/status", HTTP_GET, handleStatus);
  OTAserver.on("/schedule", HTTP_POST, handleScheduleSave);

  CLIENTID = cleanUMSG(wifipw.readUMSG(1));
  SIGN_MQTT = cleanUMSG(wifipw.readUMSG(2));
  MQTT_HOST = cleanUMSG(wifipw.readUMSG(3));
  String portStr = cleanUMSG(wifipw.readUMSG(4));
  MQTT_USER = cleanUMSG(wifipw.readUMSG(5));
  MQTT_PASS = cleanUMSG(wifipw.readUMSG(6));

  // 模式判断：如果填了自建密码(umsg6)或者端口显式填了1883等普通端口，或者明确填写了自定义局域网IP
  // 规则：
  // 1. 如果 MQTT_HOST 非空且不包含 "cmcconenet.cn" / "heclouds.com"，且填了 MQTT_PASS：视为自定义局域网 MQTT (明文TCP 1883)
  // 2. 否则视为 OneNET MQTTS (TLS 8883)
  int p = portStr.toInt();
  if (MQTT_HOST.length() > 0 && MQTT_HOST.indexOf("cmcconenet.cn") < 0 && MQTT_HOST.indexOf("heclouds.com") < 0 && (MQTT_PASS.length() > 0 || p == 1883)) {
    customMqtt = true;
    MQTT_PORT_N = (p > 0 && p <= 65535) ? p : 1883;
    if (CLIENTID.length() == 0 || !ykDeviceIdValid(CLIENTID.c_str())) {
      CLIENTID = "ykyc-" + String(ESP.getChipId(), HEX);
    }
    Serial.printf("Mode: Custom MQTT -> %s:%d, id=%s\n", MQTT_HOST.c_str(), MQTT_PORT_N, CLIENTID.c_str());
  } else {
    customMqtt = false;
    MQTT_PORT_N = (p > 0 && p <= 65535) ? p : 8883;
    if (CLIENTID.length() == 0 || !ykDeviceIdValid(CLIENTID.c_str())) {
      CLIENTID = "ykyc001_dev1";
    }
    if (MQTT_USER.length() == 0) {
      MQTT_USER = "MiXfbNnY6r";
    }
    Serial.printf("Mode: OneNET AIoT TLS -> dev=%s, pid=%s, port=%d\n", CLIENTID.c_str(), MQTT_USER.c_str(), MQTT_PORT_N);
  }
}
   
void loop()
{
  wifipw.Loop();
  wifipw.OTALoop();
  button_hok();
  localRestoreLoop();
  scheduleLoop();

  if (!recoveryApActive) {
    if (!wifipw.OK(true) && WiFi.getMode() == WIFI_STA) {
      if (wifiLostAt == 0) {
        wifiLostAt = millis();
      } else if (millis() - wifiLostAt > RECOVERY_ENTER_MS) {
        enterRecoveryAP();
      }
    } else {
      wifiLostAt = 0;
    }
  } else {
    if (millis() - recoveryApEnteredAt > RECOVERY_AP_MS) {
      exitRecoveryAP();
    }
  }

  if(ledTime < millis()){
    ledTime = millis() + 1000;
    led();
  }

  if(updateTime < millis()){
    updateTime = millis() + 1000;
    mqtt_update();
  }
}
