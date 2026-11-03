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

static const char provisioningHtml[] PROGMEM = "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">"
              "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
              "<title>云控智能插座配网</title>"
              "<style>"
              "body{font-family:Arial,sans-serif;margin:16px;color:#222;background:#fff}"
              ".wrap{max-width:600px;margin:0 auto}h2{text-align:center}"
              ".notice{border:1px solid #d99a00;background:#fff6d8;border-radius:10px;padding:12px;line-height:1.6}"
              ".field{margin:16px 0;text-align:left}.field label{display:block;font-weight:bold;margin-bottom:6px}"
              ".input{border:2px solid orange;padding:10px;border-radius:12px;width:100%;box-sizing:border-box;font-size:16px}"
              ".help{display:block;margin-top:5px;color:#555;font-size:14px;line-height:1.55}"
              ".button{display:block;width:100%;padding:14px 20px;font-size:20px;cursor:pointer;text-align:center;color:#fff;background-color:#4CAF50;border:none;border-radius:12px}"
              ".button:hover{background-color:#3e8e41}.foot{color:#555;font-size:14px;line-height:1.55}"
              "</style></head><body><main class=\"wrap\"><h2>云控智能插座配网</h2>"
              "<div class=\"notice\"><strong>先看这里：服务模式由“服务器”字段决定。</strong><br>"
              "OneNET AIoT：服务器留空或填官方域名，重点填写设备名称、Token 和产品 ID。<br>"
              "自建 MQTT：服务器填 broker 的裸 IP 或域名，重点填写服务器、端口、用户名和密码；Token 不使用。</div>"
              "<form action=\"htmlresult\" method=\"post\">"
              "<div class=\"field\"><label for=\"ssid\">WiFi 名称（SSID）</label>"
              "<input id=\"ssid\" class=\"input\" type=\"text\" name=\"ssid\" placeholder=\"要连接的 2.4GHz WiFi 名称\">"
              "<small class=\"help\">必填。只能连接 2.4GHz WiFi，名称必须与路由器 SSID 完全一致；本设备不支持 5GHz。</small></div>"
              "<div class=\"field\"><label for=\"pwd\">WiFi 密码</label>"
              "<input id=\"pwd\" class=\"input\" type=\"text\" name=\"pwd\" placeholder=\"无密码的开放网络可留空\">"
              "<small class=\"help\">填写上面 WiFi 的密码，注意大小写；提交前请再次核对。</small></div>"
              "<div class=\"field\"><label for=\"umsg1\">设备名称 / 设备 ID</label>"
              "<input id=\"umsg1\" class=\"input\" type=\"text\" name=\"umsg1\" placeholder=\"例如：ykyc001_dev1\">"
              "<small class=\"help\">两种模式都使用：OneNET 模式时须与平台中已创建的设备名一致；自建 MQTT 模式时用于话题 yunkong/设备ID/set、state、online。只能使用 1–32 个英文字母、数字、下划线或连字符。留空或不合法时，自建模式自动生成 ykyc-芯片ID，OneNET 模式自动使用 ykyc001_dev1。</small></div>"
              "<div class=\"field\"><label for=\"umsg2\">OneNET Token</label>"
              "<input id=\"umsg2\" class=\"input\" type=\"text\" name=\"umsg2\" placeholder=\"粘贴完整的 OneNET Token\">"
              "<small class=\"help\">仅 OneNET AIoT 模式使用。粘贴用 tools/onenet_tool.py 生成的完整 Token（以 version=2018-10-31&amp; 开头）；不要填写 AccessKey。自建 MQTT 模式会忽略此字段，可留空。</small></div>"
              "<div class=\"field\"><label for=\"umsg3\">服务器</label>"
              "<input id=\"umsg3\" class=\"input\" type=\"text\" name=\"umsg3\" placeholder=\"自建模式填裸 IP/域名；OneNET 模式留空\">"
              "<small class=\"help\">这是模式判定字段：留空或填 OneNET 官方域名 → OneNET AIoT；填其他裸 IP 或域名 → 自建 MQTT，例如 192.168.88.79。不要添加 mqtt://、端口号或路径。</small></div>"
              "<div class=\"field\"><label for=\"umsg4\">端口</label>"
              "<input id=\"umsg4\" class=\"input\" type=\"text\" name=\"umsg4\" placeholder=\"通常留空，自动使用默认端口\">"
              "<small class=\"help\">可以留空。留空时：OneNET 使用 8883，自建 MQTT 使用 1883。只有服务使用非默认端口时才填写；填写端口不会改变上面的模式判定。</small></div>"
              "<div class=\"field\"><label for=\"umsg5\">产品 ID / MQTT 用户名</label>"
              "<input id=\"umsg5\" class=\"input\" type=\"text\" name=\"umsg5\" placeholder=\"OneNET PID 或自建 MQTT 用户名\">"
              "<small class=\"help\">按模式二选一：OneNET 模式填产品 ID（PID），留空自动使用 MiXfbNnY6r；自建 MQTT 模式填 broker 用户名，broker 允许匿名时可留空。</small></div>"
              "<div class=\"field\"><label for=\"umsg6\">MQTT 密码</label>"
              "<input id=\"umsg6\" class=\"input\" type=\"text\" name=\"umsg6\" placeholder=\"仅自建 MQTT 模式填写\">"
              "<small class=\"help\">仅自建 MQTT 模式使用，填写与上面用户名对应的 broker 密码；OneNET 模式会忽略。用户名留空时按匿名连接，密码不会单独发送。</small></div>"
              "<div class=\"field\"><label>配网模式（mode）</label>"
              "<label><input type=\"radio\" name=\"mode\" value=\"1\" checked> 1（固定）</label>"
              "<small class=\"help\">这是 BGWiFiConfig 的配网参数，不是服务模式开关，请保持 1；OneNET 或自建 MQTT 由服务器字段决定。</small></div>"
              "<input class=\"button\" type=\"submit\" value=\"开始配网\">"
              "<p class=\"foot\">提交后，设备会保存配置并重启连接；配网热点关闭表示已进入连接流程。</p>"
              "</form></main></body></html>";

PubSubClient mqttclient;
WiFiClient plainMqttClient;
BearSSL::WiFiClientSecure tlsWifiClient;
BearSSL::X509List mqttcert(mqtt_ca_crt);

/** 断电保持继电器逻辑状态；偏移避开 EEPROM 低地址，减少与库/SDK 冲突 */
static const uint8_t kPersistMagic = 0xA7;
static const int kPersistEepromOffset = 64;
static const int kPersistEepromSize = 80;

// 配网参数（全部存 EEPROM，不含硬编码敏感信息）
String CLIENTID;     // umsg1: 设备名
String SIGN_MQTT;    // umsg2: OneNET 鉴权 Token
String MQTT_HOST;    // umsg3: 服务器域名/IP (留空自动生成 OneNET 域名)
int MQTT_PORT_N = 8883; // umsg4: 端口 (OneNET 默认 8883)
String MQTT_USER;    // umsg5: 产品 ID (PID)
String MQTT_PASS;    // umsg6: 自建 MQTT 密码
bool customMqtt = false;
int lastReportedFlag = -1;


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


// NTP 对时函数，TLS X.509 证书校验需要有效系统时间
void setClock()
{
  if (clockReady) {
    return;
  }
  configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
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
  wifipw.clearWiFi();
  delay(400);
  ESP.restart();
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
  json += ",\"mqtt_mode\":\"" + String(customMqtt ? "custom" : "onenet") + "\"";
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
    if (v >= 0 && ykWithinIgnoreWindow(millis(), mqttIgnoreRelayDownlinkUntil)) {
      // 订阅后窗口内：判定为平台重投/排队包，不执行也不回执（与自定义模式同制，
      // 见 mqtt_custom.h ykWithinIgnoreWindow）。窗口外的新指令不受影响。
      Serial.println("onenet property/set ignored: post-subscribe window");
      return;
    }
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
    mqttclient.publish(tOnline, "online", true);
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
  updateFlagStart();
  digitalWrite(LED_RED, HIGH);
  delay(100);

  Serial.println("Startup YKYC-001");

  wifipw.setCompatibleMode(true);
  wifipw.setWiFiTimeOut(10);
  wifipw.useSpaceWiFi(true);
  wifipw.autoStart(true);
  wifipw.setZDYhtml(String(FPSTR(provisioningHtml)));
  wifipw.setNumUMSG(6);
  wifipw.begin();
  wifipw.setOTATimeOut(30);
  wifipw.setOTAhtml(String(FPSTR(otaIndexHtml)));
  wifipw.OTAbegin();
  OTAserver.on("/otaclearcfg", HTTP_POST, handleOtaClearCfg);
  OTAserver.on("/status", HTTP_GET, handleStatus);

  CLIENTID = cleanUMSG(wifipw.readUMSG(1));
  SIGN_MQTT = cleanUMSG(wifipw.readUMSG(2));
  MQTT_HOST = cleanUMSG(wifipw.readUMSG(3));
  String portStr = cleanUMSG(wifipw.readUMSG(4));
  MQTT_USER = cleanUMSG(wifipw.readUMSG(5));
  MQTT_PASS = cleanUMSG(wifipw.readUMSG(6));

  // 模式判断（与 custom-mqtt/README 一致，判定只此一处 ykIsCustomBrokerHost）：
  // 服务器非空且不是 OneNET 云域名 => 自定义局域网 MQTT；留空或云域名 => OneNET AIoT (TLS 8883)。
  // 与密码、端口无关——匿名/异端口 broker 同样判自定义（金鱼审查 Major 1）。
  int p = portStr.toInt();
  if (ykIsCustomBrokerHost(MQTT_HOST.c_str())) {
    customMqtt = true;
    MQTT_PORT_N = (p > 0 && p <= 65535) ? p : 1883;
    if (CLIENTID.length() > 0 && !ykDeviceIdValid(CLIENTID.c_str())) {
      Serial.printf("invalid device id '%s', fallback\n", CLIENTID.c_str());
    }
    if (CLIENTID.length() == 0 || !ykDeviceIdValid(CLIENTID.c_str())) {
      CLIENTID = "ykyc-" + String(ESP.getChipId(), HEX);
    }
    Serial.printf("Mode: Custom MQTT -> %s:%d, id=%s\n", MQTT_HOST.c_str(), MQTT_PORT_N, CLIENTID.c_str());
  } else {
    customMqtt = false;
    MQTT_PORT_N = (p > 0 && p <= 65535) ? p : 8883;
    if (CLIENTID.length() > 0 && !ykDeviceIdValid(CLIENTID.c_str())) {
      Serial.printf("invalid device id '%s', fallback\n", CLIENTID.c_str());
    }
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
