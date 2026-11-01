#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
/** OneNET MQTT 密码串超长，默认 256 会导致 CONNECT 失败 */
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>

#define eeshow
#define eeshowM 2
#define useLittleFS
#include <BGWiFiConfig.h>
#include "mqtt_custom.h"
#include "schedule.h"

//WiFiClient wifiClient;
#define HOST "mqttstls.heclouds.com"
#define PORT 8883
#define KEEPALIVE 15
#define USER "538578"
#define SYS_STR "$sys/538578/"

//***********************主要引脚
//蓝灯
#define LED_B 12
//红灯
#define LED_RED 16
//继电器
#define FLAG 14
//按钮 D3 / 13
#define BUTTON 13

BGWiFiConfig wifipw;

String html = "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\"><title>BGWiFiConfig配网</title>"
              "<style> .input { border: 2px solid orange; padding: 10px; border-radius: 50px 20px; } .button { display: inline-block; padding: 15px 25px; font-size: 24px; cursor: pointer; text-align: center; text-decoration: none; outline: none; color: #fff; background-color: #4CAF50; border: none; border-radius: 15px; box-shadow: 0 9px #999; } .button:hover { background-color: #3e8e41 } .button:active { background-color: #3e8e41; box-shadow: 0 5px #666; transform: translateY(4px); } </style>"
              "</head><body><center><form action=\"htmlresult\" method=\"post\">"
              "<h4>WiFi名称:<input class=\"input\" type=\"text\" name=\"ssid\"></h4>"
              "<h4>WiFi密码:<input class=\"input\" type=\"text\" name=\"pwd\"></h4>"
              "<h4>输入设备ID:<input class=\"input\" type=\"text\" name=\"umsg1\"></h4>"
              "<h4>输入加密串:<input class=\"input\" type=\"text\" name=\"umsg2\"></h4>"
              "<h4>MQTT服务器(留空=原OneNET):<input class=\"input\" type=\"text\" name=\"umsg3\"></h4>"
              "<h4>MQTT端口(留空=1883):<input class=\"input\" type=\"text\" name=\"umsg4\"></h4>"
              "<h4>MQTT用户名(可留空):<input class=\"input\" type=\"text\" name=\"umsg5\"></h4>"
              "<h4>MQTT密码(可留空):<input class=\"input\" type=\"text\" name=\"umsg6\"></h4>"
              "<br>模式选择：<input type=\"radio\" name=\"mode\" value=\"1\" checked>mode:1<br>"
              "<input type=\"submit\" value=\"开始配网\"></form></body></html>";

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

PubSubClient mqttclient;

BearSSL::WiFiClientSecure wifiClient;

/* set SSL/TLS certificate */
BearSSL::X509List mqttcert(mqtt_ca_crt);

/** 断电保持继电器逻辑状态；偏移避开 EEPROM 低地址，减少与库/SDK 冲突 */
static const uint8_t kPersistMagic = 0xA7;
static const int kPersistEepromOffset = 64;
// 80 → 192：容纳偏移 128 起的时刻表区（见 schedule 存储登记）。副作用：EEPROM 镜像位置
// 随 size 变化，原版存过的继电器状态首次启动读不到（magic 不符走默认），属可接受的一次性迁移。
static const int kPersistEepromSize = 192;

//设备ID
String CLIENTID;
String SIGN_MQTT;

// 自定义 MQTT 配置（配网页 umsg3-6；umsg3 非空即启用自定义模式，判定只此一处）
// 新状态登记：由 BGWiFiConfig 持久化（与 WiFi/ID 同域），clearWiFi()/OTA 清配网时一并清除；无迁移（旧配网槽位 3-6 为空 → 自动回落 OneNET）
String MQTT_HOST;
String MQTT_USER;
String MQTT_PASS;
int MQTT_PORT_N = 1883;
bool customMqtt = false;
WiFiClient plainMqttClient;
int lastReportedFlag = -1;

// 定点时刻表状态登记：规则与时区存 EEPROM 偏移 128 起（magic 0x5C），与继电器状态（偏移 64）
// 同属本固件 EEPROM 域；写入者只有 saveSchedule()（经维护页 POST /schedule 或 MQTT schedule/set），
// 清除入口为 /otaclearcfg 与按键长按复位（均调 clearScheduleStorage()）；仅自定义 MQTT 模式启用引擎。
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
/** 订阅成功后短时间内不执行下行改继电器，避免 retained/排队包误动作 */
static unsigned long mqttIgnoreRelayDownlinkUntil = 0;

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

// Set time via NTP, as required for x.509 validation
void setClock()
{
  if (clockReady) {
    return;
  }
  configTime(0, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  unsigned long started = millis();
  while (now < 8 * 3600 * 2)
  {
    if (millis() - started > 25000UL) {
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
  gmtime_r(&now, &timeinfo);
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
  EEPROM.end();
}

void persistFlagState() {
  uint8_t bit = (uint8_t)(flagState & 1);
  int off = kPersistEepromOffset;
  EEPROM.begin(kPersistEepromSize);
  if (EEPROM.read(off) != kPersistMagic || EEPROM.read(off + 1) != bit) {
    EEPROM.write(off, kPersistMagic);
    EEPROM.write(off + 1, bit);
    EEPROM.commit();
  }
  EEPROM.end();
}

void clearPersistedFlagState() {
  int off = kPersistEepromOffset;
  EEPROM.begin(kPersistEepromSize);
  EEPROM.write(off, 0);
  EEPROM.write(off + 1, 0);
  EEPROM.write(0, 0);
  EEPROM.write(1, 0);
  EEPROM.commit();
  EEPROM.end();
}

// ---- 时刻表 EEPROM 存取（布局：[off]=magic [off+1]=时区 [off+2+i*5]=enabled,hour,minute,action,mask） ----
void loadSchedule() {
  for (int i = 0; i < SCHED_MAX_RULES; i++) {
    schedRules[i].enabled = 0;
  }
  tzOffsetHours = 8;
  EEPROM.begin(kPersistEepromSize);
  int off = kSchedEepromOffset;
  if (EEPROM.read(off) == kSchedMagic) {
    int8_t tz = (int8_t)EEPROM.read(off + 1);
    if (tz >= -12 && tz <= 14) {
      tzOffsetHours = tz;
    }
    for (int i = 0; i < SCHED_MAX_RULES; i++) {
      int base = off + 2 + i * 5;
      YkRule r;
      r.enabled = EEPROM.read(base);
      r.hour = EEPROM.read(base + 1);
      r.minute = EEPROM.read(base + 2);
      r.action = EEPROM.read(base + 3);
      r.weekMask = EEPROM.read(base + 4);
      // 逐字段消毒：越界即视为坏数据，整条禁用
      if (r.enabled && ykRuleValid(&r)) {
        schedRules[i] = r;
      }
    }
  }
  EEPROM.end();
}

void saveSchedule() {
  EEPROM.begin(kPersistEepromSize);
  int off = kSchedEepromOffset;
  EEPROM.write(off, kSchedMagic);
  EEPROM.write(off + 1, (uint8_t)tzOffsetHours);
  for (int i = 0; i < SCHED_MAX_RULES; i++) {
    int base = off + 2 + i * 5;
    EEPROM.write(base, schedRules[i].enabled);
    EEPROM.write(base + 1, schedRules[i].hour);
    EEPROM.write(base + 2, schedRules[i].minute);
    EEPROM.write(base + 3, schedRules[i].action);
    EEPROM.write(base + 4, schedRules[i].weekMask);
  }
  EEPROM.commit();
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

// 当前时刻表的标准文本形（发布/状态页共用）
int currentScheduleText(char* buf, size_t bufSize) {
  return ykFormatSchedule(schedRules, SCHED_MAX_RULES, buf, bufSize);
}

// 时刻表对时：时间未就绪时每 60s 重发一次 SNTP 请求（非阻塞），就绪返回 true
bool schedEnsureClock() {
  if (time(nullptr) >= 1700000000) {
    return true;
  }
  if (!schedClockInit || millis() - lastSchedNtpTry > 60000UL) {
    configTime(tzOffsetHours * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
    schedClockInit = true;
    lastSchedNtpTry = millis();
  }
  return false;
}

// 时刻表引擎：仅自定义 MQTT 模式。时间就绪后按本地分钟粒度判定，同一分钟只判一次。
void scheduleLoop() {
  if (!customMqtt || !wifipw.OK(true)) {
    return;
  }
  if (!schedEnsureClock()) {
    return;
  }
  time_t now = time(nullptr);
  if (!clockReady) {
    clockReady = true;  // 状态页 NTP 指示与本地时间已可用
    Serial.println("schedule clock ready");
  }
  long minuteStamp = (long)(now / 60);
  if (minuteStamp == lastSchedMinute) {
    return;
  }
  lastSchedMinute = minuteStamp;
  struct tm ti;
  localtime_r(&now, &ti);
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
    "<title>OTA / 维护</title>"
    "<style>body{font-family:sans-serif;padding:16px;max-width:480px}"
    "hr{border:none;border-top:1px solid #ccc}"
    "#status{margin-top:24px;padding:12px;background:#f5f5f5;border-radius:8px;font-size:14px}"
    "#status .ok{color:#27ae60}#status .err{color:#e74c3c}"
    "#status td{padding:4px 8px}#status td:first-child{color:#666}"
    "</style></head><body>"
    "<h3>固件 OTA</h3>"
    "<form method=\"POST\" action=\"/otaupdate\" enctype=\"multipart/form-data\""
    "onsubmit=\"stopPolling();document.getElementById('status').innerHTML='升级中，请等待…'\">"
    "<input type=\"file\" name=\"otaupdate\"><br><br>"
    "<input type=\"submit\" value=\"开始升级\"></form>"
    "<hr><h3>恢复配网</h3>"
    "<p>清除已保存的 WiFi、设备 ID 与 sign，重启后进入热点配网。</p>"
    "<form method=\"POST\" action=\"/otaclearcfg\" "
    "onsubmit=\"return confirm('确定清除配网？将断网直至重新配网。');\">"
    "<button type=\"submit\" style=\"background:#c0392b;color:#fff;padding:10px 16px;border:none\""
    ">清除配网并重启</button></form>"
    "<hr><h3>定时开关（自定义MQTT模式生效）</h3>"
    "<form method=\"POST\" action=\"/schedule\">"
    "<p>时刻表：<input id=\"sched\" name=\"sched\" style=\"width:92%\" placeholder=\"0730,1,127;1730,0,127\"></p>"
    "<p style=\"font-size:12px;color:#666\">格式 HHMM,开1/关0,星期掩码（127=每天，31=工作日，96=周末），多条以 ; 分隔，最多4条，留空=清空</p>"
    "<p>时区偏移（小时）：<input id=\"tz\" name=\"tz\" size=\"4\" value=\"8\"></p>"
    "<input type=\"submit\" value=\"保存时刻表\"></form>"
    "<hr><h3>实时状态</h3>"
    "<div id=\"status\">加载中…</div>"
    "<script>"
    "var pollTimer;"
    "function stopPolling(){if(pollTimer){clearTimeout(pollTimer);pollTimer=null;}}"
    "function ok(v){return '<span class=ok>'+v+' ✓</span>'}"
    "function err(v){return '<span class=err>'+v+' ✗</span>'}"
    "function fetchStatus(){"
    "var x=new XMLHttpRequest();x.open('GET','/status');x.timeout=4000;"
    "x.onload=function(){"
    "try{var s=JSON.parse(x.responseText);"
    "var h='<table>';"
    "h+='<tr><td>WiFi</td><td>'+(s.wifi_status==3?ok(s.wifi_str):err(s.wifi_str))+'</td></tr>';"
    "h+='<tr><td>IP</td><td>'+(s.ip||'—')+'</td></tr>';"
    "h+='<tr><td>MQTT</td><td>'+(s.mqtt_ready?ok('OK'):err('NO'))+'</td></tr>';"
    "h+='<tr><td>NTP</td><td>'+(s.clock_ready?ok('OK'):err('NO'))+'</td></tr>';"
    "h+='<tr><td>继电器</td><td>'+(s.flag_state==1?'开（蓝灯）':'关（红灯）')+'</td></tr>';"
    "h+='<tr><td>重连间隔</td><td>'+((s.reconnect_delay_ms||0)/1000)+'s</td></tr>';"
    "h+='<tr><td>ClientID</td><td style=font-size:11px>'+(s.clientid||'—')+'</td></tr>';"
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

// 维护页保存时刻表：解析失败整表拒绝（400），成功落盘并回发 retained 时刻表
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
  schedClockInit = false;  // 时区可能已变，下一轮重新对时
  lastSchedMinute = -1;
  if (customMqtt && mqttclient_state == 1) {
    publishSchedule();
  }
  OTAserver.send(200, "text/html; charset=utf-8",
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>OK</title></head><body>"
    "<p>时刻表已保存。</p></body></html>");
}

void handleStatus() {
  String json = "{";
  json += "\"wifi_status\":" + String(WiFi.status());
  json += ",\"wifi_str\":\"" + String(wifiStatusStr(WiFi.status())) + "\"";
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"mqtt_ready\":" + String(mqttclient_state == 1 ? "true" : "false");
  json += ",\"clock_ready\":" + String(clockReady ? "true" : "false");
  json += ",\"flag_state\":" + String(flagState == HIGH ? "1" : "0");
  json += ",\"clientid\":\"" + CLIENTID + "\"";
  json += ",\"mqtt_mode\":\"" + String(customMqtt ? "custom" : "onenet") + "\"";
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
  // 开始更新开关状态
  if(flagState == LOW){
    // 继电器常开
    digitalWrite(FLAG, LOW);
  }else if(flagState == HIGH){
    // 继电器常闭
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
  // 最长 7 天，避免异常时间导致溢出或超长等待
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

void receivedCallback(char* topicname, uint8_t* payload, unsigned int length)
{
  // 自定义 MQTT 模式：认 yunkong/<设备ID>/set 与 .../schedule/set 两个 topic
  if (customMqtt) {
    char tSet[96];
    if (ykBuildTopic(tSet, sizeof(tSet), CLIENTID.c_str(), "set") > 0 && strcmp(topicname, tSet) == 0) {
      int v = ykParseSwitchPayload(payload, length);
      Serial.print("custom mqtt set -> ");
      Serial.println(v);
      if (v >= 0) {
        flagState = v ? HIGH : LOW;
        clearLocalRestore("custom_mqtt_set");
        updateFlagStart();
        persistFlagState();
      }
      return;
    }
    char tSchedSet[112];
    if (ykBuildTopic(tSchedSet, sizeof(tSchedSet), CLIENTID.c_str(), "schedule/set") > 0 &&
        strcmp(topicname, tSchedSet) == 0) {
      // 整体替换语义：解析成功才生效并落盘，失败保持原表并回发原表
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
  char cmdRequestPrefix[96];
  snprintf(cmdRequestPrefix, sizeof(cmdRequestPrefix), "%s%s/cmd/request/", SYS_STR, CLIENTID.c_str());

  Serial.println("----------------------------------------------------------------------");
  Serial.println(length);
  Serial.print(" ）Message received: ");
  Serial.println(topicname);
  Serial.print("payload size: ");
  Serial.println(length);
  Serial.println("\n----------------------------------------------------------------------");
  // 处理消息 strstr(topicname, CMDREQUEST); 返回指向 str1 中第一次出现 str2 的指针，如果 str2 不是 str1 的一部分，则返回空指针。
  char* res = strstr(topicname, cmdRequestPrefix);
  if(res != NULL)
  {
    if (length == 0 || length > 4) {
      Serial.println("payload invalid length");
      return;
    }
    // 解析消息
    int mIdHeight = 0;
    for (unsigned int index = 1; index < length; index++) {
      mIdHeight = mIdHeight | ((payload[index] & 0xff) << (index * 8));
    }
    timeOut = (payload[0] & 0x1f) | (mIdHeight >> 3);
    int flagStateTmp = (payload[0] >> 5) & 0x7;
    if(flagStateTmp < 2){
      const bool allow = (long)(millis() - mqttIgnoreRelayDownlinkUntil) >= 0;
      if (allow) {
        flagState = flagStateTmp;
        if (timeOut > 0) {
          armLocalRestore(flagStateTmp, timeOut);
        } else {
          clearLocalRestore("no_delay");
        }
      }
    }
    Serial.print("+ type:");
    Serial.println(flagStateTmp);
    Serial.print("+ timeOut:");
    Serial.println(timeOut);
    Serial.println("***************************");
    updateFlagStart();
    if (flagStateTmp < 2 && (long)(millis() - mqttIgnoreRelayDownlinkUntil) >= 0) {
      persistFlagState();
    }

    // 回复平台下发的指令，回复内容可自定义
    char topic[160];
    const char* cmdid = &topicname[strlen(cmdRequestPrefix)];
    snprintf(topic, sizeof(topic), "%s%s/cmd/response/%s", SYS_STR, CLIENTID.c_str(), cmdid);
    char result[2] = {'0', '\0'};
    result[0] = flagState == HIGH ? '1' : '0';
    mqttclient.publish(topic, result);
  }
}

// MQTT 重连退避一步：按当前间隔安排下次连接，并将间隔翻倍（上限封顶）
void mqttBackoff() {
  updateTime = millis() + mqttReconnectDelayMs;
  mqttReconnectDelayMs = mqttReconnectDelayMs * 2;
  if (mqttReconnectDelayMs > MQTT_RECONNECT_MAX_MS) {
    mqttReconnectDelayMs = MQTT_RECONNECT_MAX_MS;
  }
}

// 自定义 MQTT：retained 发布当前时刻表文本（连接时与每次变更时）
void publishSchedule() {
  char topic[96];
  char text[128];
  if (ykBuildTopic(topic, sizeof(topic), CLIENTID.c_str(), "schedule") > 0 &&
      currentScheduleText(text, sizeof(text)) >= 0) {
    mqttclient.publish(topic, text, true);
  }
}

// 自定义 MQTT：上报当前开关状态（retained），并登记已上报值
void publishCustomState() {
  char topic[96];
  if (ykBuildTopic(topic, sizeof(topic), CLIENTID.c_str(), "state") > 0) {
    mqttclient.publish(topic, flagState == HIGH ? "ON" : "OFF", true);
    lastReportedFlag = flagState;
  }
}

// 自定义 MQTT 主循环：明文 TCP + 普通账密（或匿名），LWT 维护 online 状态。
// 重连退避复用 OneNET 路径的全局变量与节奏，两条路径行为一致。
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
    // 物理按键等本地改动也经此处统一上报（单一上报点）
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

void mqtt_update() {
  if (customMqtt) {
    mqtt_update_custom();
    return;
  }
  if(!wifipw.OK(true))
  {
    Serial.print("NOT CONNECTED ->>");
    Serial.println(wifipw.retWiFiSET());
    updateTime = millis() + 1000;
    return;
  }
  // mqtt 连接成功
  if(mqttclient_state == 1){
    Serial.print("mqttclient.loop ->>");
    Serial.println(wifipw.retWiFiSET());
    // 此函数将侦听传入的订阅topic-process-invoke receivedCallback
    if (!mqttclient.loop())
    {
      Serial.println(WiFi.status());
      Serial.println("mqttclient.loop fail!");
      mqttclient_state = 0;
      updateTime = millis() + mqttReconnectDelayMs;
      // resetFunc();
    }
    return;
  }
  // 连接 mqtt
  Serial.println(wifipw.retWiFiSET());
  // 命令接收
  char cmdRequestSub[96];
  snprintf(cmdRequestSub, sizeof(cmdRequestSub), "%s%s/cmd/request/+", SYS_STR, CLIENTID.c_str());
  // 命令回复结果
  char cmdResponseSub[96];
  snprintf(cmdResponseSub, sizeof(cmdResponseSub), "%s%s/cmd/response/+/+", SYS_STR, CLIENTID.c_str());
  char pwd[220];
  snprintf(pwd, sizeof(pwd), "version=2018-10-31&res=products%%2F538578%%2Fdevices%%2F%s&et=1924963199&method=md5&sign=%s", CLIENTID.c_str(), SIGN_MQTT.c_str());

  if (mqttclient.connected()) {
    mqttclient.disconnect();
  } else {
    wifiClient.stop();
  }
  delay(50);

  wifiClient.setTrustAnchors(&mqttcert);
  setClock();
  if (!clockReady) {
    updateTime = millis() + 1000;
    return;
  }
  IPAddress mqttIp;
  if (!WiFi.hostByName(HOST, mqttIp)) {
    Serial.printf("DNS failed for %s\n", HOST);
    updateTime = millis() + 2000;
    return;
  }
  wifiClient.setX509Time(time(nullptr));
  wifiClient.setBufferSizes(8192, 512);
  mqttclient.setClient(wifiClient);
  mqttclient.setServer(mqttIp, PORT);
  mqttclient.setKeepAlive(KEEPALIVE);
  mqttclient.setSocketTimeout(10);
  mqttclient.setCallback(receivedCallback);
  // STEP5: Connect to UCloud MQTT Broker
  if (mqttclient.connect(CLIENTID.c_str(), USER, pwd))
  {
    mqttclient_state = 1;
    mqttReconnectDelayMs = MQTT_RECONNECT_MIN_MS;
    mqttclient.subscribe(cmdRequestSub,1);
    mqttclient.subscribe(cmdResponseSub,1);
    mqttIgnoreRelayDownlinkUntil = millis() + 2500UL;
    Serial.println("static connected");
    Serial.println(cmdRequestSub);
    Serial.println(cmdResponseSub);
    updateTime = millis() + 500;
  }
  else
  {
    Serial.printf("static connect failed, status code = %d\n", mqttclient.state());
    updateTime = millis() + mqttReconnectDelayMs;
    mqttReconnectDelayMs = mqttReconnectDelayMs * 2;
    if (mqttReconnectDelayMs > MQTT_RECONNECT_MAX_MS) {
      mqttReconnectDelayMs = MQTT_RECONNECT_MAX_MS;
    }
  }
}

void led(){
  // 灯光处理
  if(flagState == HIGH){
    if(rLedState){
      // 红灯亮就关闭
      rLedState = false;
      digitalWrite(LED_RED, LOW);
    }
    // 继电器常开 蓝灯
    if(wifipw.OK(true)){
      // 联网 蓝灯常亮, 蓝灯不亮就点亮
      if(!bLedState){
        bLedState = true;
        digitalWrite(LED_B, HIGH);
      }
    }else{
      // 未联网  蓝灯闪亮， 切换状态
      bLedState = !bLedState;
      digitalWrite(LED_B, bLedState);
    }
  }else{
    if(bLedState){
      // 蓝灯亮就关闭
      bLedState = false;
      digitalWrite(LED_B, LOW);
    }
    // 继电器常闭 红灯
    if(wifipw.OK(true)){
      // 联网 红灯常亮, 红灯不亮就点亮
      if(!rLedState){
        rLedState = true;
        digitalWrite(LED_RED, HIGH);
      }
    }else{
      // 未联网 红灯闪亮, 切换状态
      rLedState = !rLedState;
      digitalWrite(LED_RED, rLedState);
    }

  }
}

void button_hok(){
  int buttonStateTmp = digitalRead(BUTTON);
  // 按钮状态高 表示松开
  if (buttonStateTmp == HIGH) {
    // 松开 
    if(buttonState == HIGH){
      // 按钮状态改变
      buttonState = LOW;
      unsigned long downTime = millis() - buttonMillis;
      if(downTime < 50){

      }
      else if(downTime < 3000)
      {
        Serial.println("buttonState HIGH < 3s");
        flagState = HIGH == flagState? LOW: HIGH;
        clearLocalRestore("manual_button_toggle");
        updateFlagStart();
        persistFlagState();
      }
      else
      {
        Serial.println("buttonState HIGH > 3s");
        Serial.println("reset");
        clearPersistedFlagState();
        clearScheduleStorage();
        wifipw.clearWiFi();
      }
    }
  } else {
    // 按下
    if(buttonState == LOW){
      // 按钮状态改变, 记录时间点
      buttonState = HIGH;
      buttonMillis = millis();
    }else{
      Serial.print("按下时间：");
      Serial.print(millis() - buttonMillis);
      Serial.println("ms");
    }
  }
}

//declare reset function at address 0
void(* resetFunc) (void) = 0; 

void setup()
{
  Serial.begin(115200);
  delay(1000);
  mqttclient.setBufferSize(512);
  mqttclient.setClient(wifiClient);

  // 初始化引脚
  pinMode(LED_B, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(FLAG, OUTPUT);
  pinMode(BUTTON, INPUT);
  loadPersistedFlagState();
  loadSchedule();
  updateFlagStart();
  //默认点亮红灯
  digitalWrite(LED_RED, HIGH);
	delay(100);

  // STEP2: Do SmartConifg and connecting to a WiFi network
	delay(10);
	Serial.println("Startup");

  wifipw.setCompatibleMode(true);
  /** 勿用 setConFailReset：弱网/路由器慢易误判反复重启甚至清配网 */
  wifipw.setWiFiTimeOut(10);  // 10s 超时即进入 loop，按钮可用，后台继续连
  wifipw.useSpaceWiFi(true);
  wifipw.autoStart(true);
  wifipw.setZDYhtml(html);
  wifipw.setNumUMSG(6);
  //优化解决部分手机配网WiFi自动断连问题，传入true时启用兼容模式，配网IP将从192.168.22.22变为192.168.22.1。
  // wifipw.setCompatibleMode(true);      
  wifipw.begin();
  wifipw.setOTATimeOut(30);
  wifipw.setOTAhtml(String(FPSTR(otaIndexHtml)));
  wifipw.OTAbegin();
  OTAserver.on("/otaclearcfg", HTTP_POST, handleOtaClearCfg);
  OTAserver.on("/status", HTTP_GET, handleStatus);
  OTAserver.on("/schedule", HTTP_POST, handleScheduleSave);

  CLIENTID = wifipw.readUMSG(1);
  SIGN_MQTT = wifipw.readUMSG(2);
	Serial.println(CLIENTID);
	Serial.println(SIGN_MQTT);

  // 自定义 MQTT 配置：BGWiFiConfig 未设置的槽位返回 "NULL"，一律按空处理
  MQTT_HOST = wifipw.readUMSG(3);
  String portStr = wifipw.readUMSG(4);
  MQTT_USER = wifipw.readUMSG(5);
  MQTT_PASS = wifipw.readUMSG(6);
  if (MQTT_HOST == "NULL") MQTT_HOST = "";
  if (portStr == "NULL") portStr = "";
  if (MQTT_USER == "NULL") MQTT_USER = "";
  if (MQTT_PASS == "NULL") MQTT_PASS = "";
  MQTT_HOST.trim();
  customMqtt = MQTT_HOST.length() > 0;
  if (customMqtt) {
    int p = portStr.toInt();
    MQTT_PORT_N = (p > 0 && p <= 65535) ? p : 1883;
    if (CLIENTID.length() == 0 || CLIENTID == "NULL") {
      CLIENTID = "ykyc-" + String(ESP.getChipId(), HEX);
    }
    Serial.print("custom mqtt -> ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.print(MQTT_PORT_N);
    Serial.print(" id=");
    Serial.println(CLIENTID);
  }
}
   
void loop()
{
  wifipw.Loop();
  wifipw.OTALoop();
  button_hok();
  localRestoreLoop();
  scheduleLoop();

  // --- WiFi 离线恢复：仅 STA 模式断连时触发，配网模式下不触发 ---
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
    Serial.println("updateTime");
    mqtt_update();
  }
}
