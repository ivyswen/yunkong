#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
/** OneNET MQTT 密码很长，默认 256 会导致 CONNECT 发不出去、一直连不上 */
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>

#define eeshow
/** 2：首页 "/" 即自定义配网页（含设备 ID + sign）；1 时首页是库默认页，设备项在 /html */
#define eeshowM 2
#define useLittleFS
#include <BGWiFiConfig.h>

#define HOST "mqttstls.heclouds.com"
#define PORT 8883
#define KEEPALIVE 15
#define USER "538578"
#define SYS_STR "$sys/538578/"

// ESP-01S: GPIO0 接继电器（光耦低电平吸合），GPIO2 板载 LED（低电平亮）
#define RELAY_PIN 0
#define LED_PIN 2

BGWiFiConfig wifipw;
PubSubClient mqttclient;
BearSSL::WiFiClientSecure wifiClient;

String html = "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\"><title>ESP01S 配网</title>"
              "<style>body{font-family:Arial;padding:12px}input{width:95%;padding:8px;margin:6px 0}button{padding:10px 16px}</style>"
              "</head><body><h3>ESP-01S 配网</h3><form action=\"htmlresult\" method=\"post\">"
              "<label>WiFi 名称</label><input type=\"text\" name=\"ssid\">"
              "<label>WiFi 密码</label><input type=\"text\" name=\"pwd\">"
              "<label>OneNET 设备名称 / 设备ID</label><input type=\"text\" name=\"umsg1\" placeholder=\"与平台设备名一致\">"
              "<label>加密串 sign（与 12F 配网一致，填小程序/后端返回的 devicePassword）</label><input type=\"text\" name=\"umsg2\" placeholder=\"同 12F「输入加密串」\">"
              "<button type=\"submit\">保存并重启</button></form></body></html>";

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
BearSSL::X509List mqttcert(mqtt_ca_crt);

/** 断电保持开关逻辑状态；偏移避开 EEPROM 低地址，减少与库/SDK 冲突 */
static const uint8_t kPersistMagic = 0xA7;
static const int kPersistEepromOffset = 64;
static const int kPersistEepromSize = 80;

String CLIENTID;
String SIGN_MQTT;

/** 与 12F 相同：协议 0/1 写入后为 LOW/HIGH（见 receivedCallback）；继电器引脚在 updateFlagStart 映射 */
int flagState = HIGH;
unsigned long timeOut = 0;
bool restoreArmed = false;
unsigned long restoreAtMillis = 0;
int restoreTargetState = HIGH;

unsigned long mqttTickAt = 0;
unsigned long ledTickAt = 0;
unsigned long mqttReconnectDelayMs = 2000;
const unsigned long MQTT_RECONNECT_MIN_MS = 2000;
const unsigned long MQTT_RECONNECT_MAX_MS = 60000;
bool mqttReady = false;
bool clockReady = false;
/** 订阅成功后短时间内不执行下行改继电器，避免 retained/排队包误判 */
static unsigned long mqttIgnoreRelayDownlinkUntil = 0;

// WiFi 离线恢复：STA 断连 2 分钟 → 进入 AP，AP 2 分钟 → 切回 STA 重试
unsigned long wifiLostAt = 0;
bool recoveryApActive = false;
unsigned long recoveryApEnteredAt = 0;
const unsigned long RECOVERY_ENTER_MS = 120000;
const unsigned long RECOVERY_AP_MS = 120000;

// 声明 reset 函数指针
void(* resetFunc) (void) = 0;

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

/** 串口诊断：参数与网络状态（MQTT state 见 PubSubClient 文档，-4 超时等） */
void logDiag(const char* reason) {
  Serial.println();
  Serial.printf("[Diag] %s\r\n", reason);
  Serial.printf("  WiFi.status=%d (%s)  IP=%s  gw=%s\r\n",
                (int)WiFi.status(), wifiStatusStr((int)WiFi.status()),
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());
  {
    String r = wifipw.retWiFiSET();
    Serial.printf("  wifipw.OK(true)=%d  retWiFiSET=%s\r\n", (int)wifipw.OK(true), r.c_str());
  }

  String ssid = wifipw.readWiFi(0);
  String pwd = wifipw.readWiFi(1);
  Serial.printf("  stored ssid(len=%u)=[%s]\r\n", (unsigned)ssid.length(), ssid.c_str());
  Serial.printf("  stored pwd len=%u (密码不打印)\r\n", (unsigned)pwd.length());

  Serial.printf("  MQTT host=%s:%d  mqttReady=%d  ntp/clockReady=%d\r\n",
                HOST, PORT, (int)mqttReady, (int)clockReady);
  Serial.printf("  USER(product)=[%s]\r\n", USER);
  Serial.printf("  CLIENTID/设备名(len=%u)=[%s]\r\n",
                (unsigned)CLIENTID.length(), CLIENTID.c_str());
  Serial.printf("  SIGN(len=%u)", (unsigned)SIGN_MQTT.length());
  if (SIGN_MQTT.length() > 0) {
    Serial.print(" head[");
    for (unsigned i = 0; i < 8 && i < SIGN_MQTT.length(); i++) {
      Serial.print(SIGN_MQTT[i]);
    }
    Serial.print("] tail[");
    int start = (int)SIGN_MQTT.length() - 8;
    if (start < 0) start = 0;
    for (int i = start; i < (int)SIGN_MQTT.length(); i++) {
      Serial.print(SIGN_MQTT[i]);
    }
    Serial.print(']');
  }
  Serial.println();

  if (CLIENTID.length() == 0) {
    Serial.println(F("  !! CLIENTID 为空：配网页 umsg1 未写入或损坏"));
  }
  if (SIGN_MQTT.length() == 0) {
    Serial.println(F("  !! SIGN 为空：配网页 umsg2 未写入或损坏"));
  }
  if (SIGN_MQTT.indexOf('&') >= 0 || SIGN_MQTT.indexOf("version=") >= 0) {
    Serial.println(F("  !! SIGN 疑似整段 token，只填 Base64 或已 URL 编码的 sign"));
  }
  if (SIGN_MQTT.indexOf('%') < 0 && SIGN_MQTT.indexOf('=') >= 0) {
    Serial.println(F("  !! umsg2 为控制台「原版 Base64」时，BGWiFiConfig 用 =umsg= 截断易吃掉末尾=→sign错误"));
    Serial.println(F("     请优先用小程序复制的 sign（含%3D）；或清除配网后只填 URL 编码串"));
  }
  if (SIGN_MQTT.length() > 0 && (unsigned)SIGN_MQTT.length() < 32) {
    Serial.println(F("  !! SIGN 偏短：若 12F 串口打印更长，说明 umsg2 被截断，请清除配网用小复制「加密串」重填"));
    Serial.println(F("     注：state=-2 多为 TCP/TLS，未必是 sign；state=4/5 才优先怀疑鉴权/截断"));
  }
  Serial.printf("  PubSubClient bufferSize=%u\r\n", (unsigned)mqttclient.getBufferSize());
}

/** OTA 首页（配网 + 实时状态轮询 + 清除配网），须先于 OTAbegin */
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
    "h+='<tr><td>开关</td><td>'+(s.flag_state==1?'开':'关')+'</td></tr>';"
    "h+='<tr><td>重连间隔</td><td>'+((s.reconnect_delay_ms||0)/1000)+'s</td></tr>';"
    "h+='<tr><td>ClientID</td><td style=font-size:11px>'+(s.clientid||'—')+'</td></tr>';"
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

void handleStatus() {
  String json = "{";
  json += "\"wifi_status\":" + String(WiFi.status());
  json += ",\"wifi_str\":\"" + String(wifiStatusStr(WiFi.status())) + "\"";
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"mqtt_ready\":" + String(mqttReady ? "true" : "false");
  json += ",\"clock_ready\":" + String(clockReady ? "true" : "false");
  json += ",\"flag_state\":" + String(flagState == HIGH ? "1" : "0");
  json += ",\"clientid\":\"" + CLIENTID + "\"";
  json += ",\"reconnect_delay_ms\":" + String(mqttReconnectDelayMs);
  json += "}";
  OTAserver.sendHeader("Access-Control-Allow-Origin", "*");
  OTAserver.send(200, "application/json", json);
}

void enterRecoveryAP() {
  WiFi.softAP("ESP01S_Recovery", "");
  OTAserver.begin();
  recoveryApActive = true;
  recoveryApEnteredAt = millis();
  wifiLostAt = 0;
}

void exitRecoveryAP() {
  WiFi.softAPdisconnect(true);
  recoveryApActive = false;
}

/** ESP-01S: 光耦继电器低电平吸合；LED 低电平亮 */
void updateFlagStart() {
  digitalWrite(RELAY_PIN, flagState == HIGH ? LOW : HIGH);
  digitalWrite(LED_PIN, flagState == HIGH ? LOW : HIGH);
}

void clearLocalRestore(const char* reason) {
  restoreArmed = false;
  restoreAtMillis = 0;
  Serial.print(F("clearLocalRestore -> "));
  Serial.println(reason);
}

void armLocalRestore(int currentCmdState, unsigned long delaySec) {
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
  Serial.print(F("armLocalRestore -> delaySec="));
  Serial.print(delaySec);
  Serial.print(F(", target="));
  Serial.println(restoreTargetState);
}

void localRestoreLoop() {
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
  Serial.println(F("localRestoreLoop -> restored"));
}

/**
 * TLS 校验证书需要可信 UTC。ESP-01 上 NTP 常慢于 10s，原先超时仍去连 MQTT → BearSSL 校验失败 → state=-2。
 * 使用 offset=0 + 国内 NTP；未同步成功前 mqttUpdate 不会去握手。
 */
void setClock() {
  if (clockReady) {
    return;
  }
  configTime(0, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
  time_t now = time(nullptr);
  unsigned long started = millis();
  while (now < 8 * 3600 * 2) {
    if (millis() - started > 25000UL) {
      Serial.println(F("[Diag] NTP 25s 内未就绪，无法校验证书，暂不连 MQTT"));
      return;
    }
    delay(400);
    yield();
    now = time(nullptr);
  }
  clockReady = true;
  Serial.println(F("[Diag] NTP 已同步，开始 MQTT TLS"));
}

void receivedCallback(char* topicname, uint8_t* payload, unsigned int length) {
  char prefix[96];
  snprintf(prefix, sizeof(prefix), "%s%s/cmd/request/", SYS_STR, CLIENTID.c_str());
  if (strstr(topicname, prefix) == NULL) {
    return;
  }
  if (length == 0 || length > 4) {
    return;
  }
  int mIdHeight = 0;
  for (unsigned int index = 1; index < length; index++) {
    mIdHeight = mIdHeight | ((payload[index] & 0xff) << (index * 8));
  }
  timeOut = (payload[0] & 0x1f) | (mIdHeight >> 3);
  int flagStateTmp = (payload[0] >> 5) & 0x7;
  if (flagStateTmp < 2) {
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
  updateFlagStart();
  if (flagStateTmp < 2 && (long)(millis() - mqttIgnoreRelayDownlinkUntil) >= 0) {
    persistFlagState();
  }

  char topic[160];
  const char* cmdid = &topicname[strlen(prefix)];
  snprintf(topic, sizeof(topic), "%s%s/cmd/response/%s", SYS_STR, CLIENTID.c_str(), cmdid);
  char result[2] = {'0', '\0'};
  result[0] = flagState == HIGH ? '1' : '0';
  mqttclient.publish(topic, result);
}

void mqttUpdate() {
  static unsigned long lastWifiDiag = 0;
  static unsigned long lastMqttFailDiag = 0;
  const unsigned long kDiagIntervalMs = 12000;

  if (!wifipw.OK(true)) {
    mqttReady = false;
    mqttTickAt = millis() + 1000;
    if (millis() - lastWifiDiag >= kDiagIntervalMs) {
      lastWifiDiag = millis();
      logDiag("WiFi 未连上（每约12s重复）");
    }
    return;
  }

  if (mqttReady) {
    if (!mqttclient.loop()) {
      mqttReady = false;
      mqttTickAt = millis() + mqttReconnectDelayMs;
      if (millis() - lastMqttFailDiag >= kDiagIntervalMs) {
        lastMqttFailDiag = millis();
        Serial.printf("[Diag] mqttclient.loop 失败 state=%d\r\n", mqttclient.state());
        logDiag("MQTT 已断开");
      }
    }
    return;
  }

  char subReq[96];
  char subResp[96];
  char pwd[220];
  snprintf(subReq, sizeof(subReq), "%s%s/cmd/request/+", SYS_STR, CLIENTID.c_str());
  snprintf(subResp, sizeof(subResp), "%s%s/cmd/response/+/+", SYS_STR, CLIENTID.c_str());
  snprintf(pwd, sizeof(pwd),
           "version=2018-10-31&res=products%%2F538578%%2Fdevices%%2F%s&et=1924963199&method=md5&sign=%s",
           CLIENTID.c_str(), SIGN_MQTT.c_str());

  /** BearSSL：接收缓冲过小会导致 Certificate 记录装不下，握手失败 → PubSub state=-2（≠鉴权错误） */
  if (mqttclient.connected()) {
    mqttclient.disconnect();
  } else {
    wifiClient.stop();
  }
  delay(50);

  wifiClient.setTrustAnchors(&mqttcert);
  setClock();
  if (!clockReady) {
    mqttTickAt = millis() + 1000;
    return;
  }

  /** 证书 CN=OneNET MQTTS，不含 mqttstls.heclouds.com；域名连接触发 BearSSL 主机名校验 → BAD_SERVER_NAME */
  IPAddress mqttIp;
  if (!WiFi.hostByName(HOST, mqttIp)) {
    mqttTickAt = millis() + 2000;
    if (millis() - lastMqttFailDiag >= kDiagIntervalMs) {
      lastMqttFailDiag = millis();
      Serial.printf("[Diag] DNS 解析失败 host=%s\r\n", HOST);
    }
    return;
  }
  wifiClient.setX509Time(time(nullptr));

  /** ESP8266 核心默认 16384/512，见 WiFiClientSecureBearSSL.cpp「Minimum safe」；勿再用 1KB 级 recv */
  wifiClient.setBufferSizes(16384, 512);

  mqttclient.setClient(wifiClient);
  mqttclient.setServer(mqttIp, PORT);
  mqttclient.setKeepAlive(KEEPALIVE);
  mqttclient.setSocketTimeout(15);
  mqttclient.setCallback(receivedCallback);

  const bool okMqtt = mqttclient.connect(CLIENTID.c_str(), USER, pwd);
  if (!okMqtt) {
    char sslMsg[88] = {0};
    wifiClient.getLastSSLError(sslMsg, sizeof(sslMsg));
    if (sslMsg[0]) {
      Serial.printf("[Diag] BearSSL(握手失败后): %s\r\n", sslMsg);
    }
  }
  if (okMqtt) {
    mqttReady = true;
    mqttReconnectDelayMs = MQTT_RECONNECT_MIN_MS;
    mqttclient.subscribe(subReq, 1);
    mqttclient.subscribe(subResp, 1);
    mqttIgnoreRelayDownlinkUntil = millis() + 2500UL;
    mqttTickAt = millis() + 500;
    Serial.println(F("[Diag] MQTT 已连接并订阅（约2.5s 内忽略下行改继电器/persist，仅回当前状态）"));
  } else {
    if (millis() - lastMqttFailDiag >= kDiagIntervalMs) {
      lastMqttFailDiag = millis();
      int st = mqttclient.state();
      Serial.printf("[Diag] MQTT connect 失败 state=%d（-2=TCP/TLS 未成功；4/5=CONNECT 拒识，才查密码/sign）\r\n", st);
      Serial.printf("  pwdLen=%u buf=%u heapFree=%u\r\n", (unsigned)strlen(pwd),
                    (unsigned)mqttclient.getBufferSize(), ESP.getFreeHeap());
      logDiag("MQTT connect fail");
    }
    mqttTickAt = millis() + mqttReconnectDelayMs;
    mqttReconnectDelayMs = mqttReconnectDelayMs * 2;
    if (mqttReconnectDelayMs > MQTT_RECONNECT_MAX_MS) {
      mqttReconnectDelayMs = MQTT_RECONNECT_MAX_MS;
    }
  }
}

void led() {
  // ESP-01S: GPIO2 板载 LED，低电平亮
  if (wifipw.OK(true)) {
    // 联网：LED 反映开关状态（开→亮，关→灭）
    digitalWrite(LED_PIN, flagState == HIGH ? LOW : HIGH);
  } else {
    // 未联网：LED 闪烁
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  /** PubSubClient 库里默认 256；Arduino 单独编库时 sketch 的 MQTT_MAX_PACKET_SIZE 可能不生效，必须运行时调大 */
  mqttclient.setBufferSize(768);
  /** 必须在首次 mqttUpdate/disconnect 前绑定，否则 disconnect() 内部 _client->write 空指针崩溃 */
  mqttclient.setClient(wifiClient);

  Serial.println();
  Serial.println(F("OTA：浏览器访问设备 IP，升级页底部可「清除配网」"));

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  loadPersistedFlagState();
  updateFlagStart();

  wifipw.setCompatibleMode(true);
  /** 勿用 setConFailReset(2,10,true)：库在 STA_M3_FailReset 里「失败 >2 轮」会 clearWiFi()+ESP.restart()，
   *  单次 STA_M1 仅等 SECtime 秒，弱网/路由器慢易被当成失败 → 反复自动重启、甚至清空配网。 */
  wifipw.setWiFiTimeOut(45);
  wifipw.useSpaceWiFi(true);
  wifipw.autoStart(true);
  wifipw.setZDYhtml(html);
  wifipw.setNumUMSG(2); // umsg1=deviceId, umsg2=sign
  wifipw.begin();
  wifipw.setOTATimeOut(30);
  wifipw.setOTAhtml(String(FPSTR(otaIndexHtml)));
  wifipw.OTAbegin();
  OTAserver.on("/otaclearcfg", HTTP_POST, handleOtaClearCfg);
  OTAserver.on("/status", HTTP_GET, handleStatus);

  CLIENTID = wifipw.readUMSG(1);
  SIGN_MQTT = wifipw.readUMSG(2);

  logDiag("上电：当前已存参数（若未连上，下面每隔约12s会再打印）");
}

void loop() {
  wifipw.Loop();
  wifipw.OTALoop();
  localRestoreLoop();

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

  if (ledTickAt < millis()) {
    ledTickAt = millis() + 1000;
    led();
  }

  if (mqttTickAt < millis()) {
    mqttTickAt = millis() + 1000;
    mqttUpdate();
  }
}
