#include <Arduino.h>
#include <EEPROM.h>
#include <MD5Builder.h>
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
#ifdef BOARD_ESP8285_PANEL
// 86 面板 ESP-M4 版（build.sh esp8285 变体）：灯色与插头版相反——
// 2026-11-05 真机：状态「关（红灯）」时实际亮蓝灯。本固件 led()
// 恒为一脚高、一脚低（选中灯亮/闪、另一灯灭），故对调引脚与整体
// 反相在现象上严格等价，引脚对调与有效电平相反两种根因下此对调
// 都成立。继电器方向未经实测，不凭灯的结论推翻（见 FLAG 定义）。
//蓝灯 GPIO16
#define LED_B 16
//红灯 GPIO12
#define LED_RED 12
#else
//蓝灯 GPIO12
#define LED_B 12
//红灯 GPIO16
#define LED_RED 16
#endif
//继电器 GPIO14
#define FLAG 14
//按钮 GPIO13
#define BUTTON 13

BGWiFiConfig wifipw;

// 固件版本：由 custom-mqtt/build.sh 在编译时注入（git 短哈希 + 编译时间），
// 用于 OTA 后在 Web 页面区分新旧固件；未经脚本编译时回落 dev + 编译日期宏。
#ifndef FW_GIT_HASH
#define FW_GIT_HASH "dev"
#endif
#ifndef FW_BUILD_TIME
#define FW_BUILD_TIME __DATE__ " " __TIME__
#endif

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
              "OneNET AIoT：服务器留空，重点填写设备名称、Token 和产品 ID。<br>"
              "自建 MQTT：服务器填 broker 的裸 IP 或域名，重点填写服务器、端口、用户名和密码；Token 不使用。</div>"
              "<form action=\"htmlresult\" method=\"post\">"
              "<div class=\"field\"><label for=\"ssid\">WiFi 名称（SSID）</label>"
              "<input id=\"ssid\" class=\"input\" type=\"text\" name=\"ssid\" placeholder=\"要连接的 2.4GHz WiFi 名称\">"
              "<small class=\"help\">必填。只能连接 2.4GHz WiFi，名称必须与路由器 SSID 完全一致；本设备不支持 5GHz。</small></div>"
              "<div class=\"field\"><label for=\"pwd\">WiFi 密码</label>"
              "<input id=\"pwd\" class=\"input\" type=\"text\" name=\"pwd\" placeholder=\"无密码的开放网络可留空\">"
              "<small class=\"help\">填写上面 WiFi 的密码，注意大小写；提交前请再次核对。</small></div>"
              "<div class=\"field\"><label for=\"umsg1\">设备名称 / 设备 ID</label>"
              "<input id=\"umsg1\" class=\"input\" type=\"text\" name=\"umsg1\" placeholder=\"例如：%DEFAULT_DEV_ID%（留空自动使用）\">"
              "<small class=\"help\">两种模式都使用：OneNET 模式时须与平台中已创建的设备名一致；自建 MQTT 模式时用于话题 yunkong/设备ID/set、state、online。只能使用 1–32 个英文字母、数字、下划线或连字符。留空或不合法时，两种模式均自动使用当前设备默认 ID（%DEFAULT_DEV_ID%）。</small></div>"
              "<div class=\"field\"><label for=\"umsg2\">OneNET Token</label>"
              "<input id=\"umsg2\" class=\"input\" type=\"text\" name=\"umsg2\" placeholder=\"粘贴完整的 OneNET Token\">"
              "<small class=\"help\">仅 OneNET AIoT 模式使用。粘贴用 tools/onenet_tool.py 生成的完整 Token（以 version=2018-10-31&amp; 开头）；不要填写 AccessKey。自建 MQTT 模式会忽略此字段，可留空。</small></div>"
              "<div class=\"field\"><label for=\"umsg3\">服务器</label>"
              "<input id=\"umsg3\" class=\"input\" type=\"text\" name=\"umsg3\" placeholder=\"自建模式填裸 IP/域名；OneNET 模式留空\">"
              "<small class=\"help\">这是模式判定字段：留空 → OneNET AIoT；填含小写 cmcconenet.cn 或 heclouds.com 的官方域名也判 OneNET；填其他裸 IP 或域名 → 自建 MQTT，例如 192.168.88.79。判定区分大小写，域名务必全小写，大写会被当成自建服务器。不要添加 mqtt://、端口号或路径。</small></div>"
              "<div class=\"field\"><label for=\"umsg4\">端口</label>"
              "<input id=\"umsg4\" class=\"input\" type=\"text\" name=\"umsg4\" placeholder=\"通常留空，自动使用默认端口\">"
              "<small class=\"help\">可以留空。留空时：OneNET 使用 8883，自建 MQTT 使用 1883。只有服务使用非默认端口时才填写，端口不影响模式判定；请只填纯数字：填 0、超过 65535 或非数字开头会自动回落默认端口，数字后混字母会被截断解析（例如 1883abc 会按 1883 用）。</small></div>"
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
              "<p class=\"foot\">固件版本：" FW_GIT_HASH " · 编译时间 " FW_BUILD_TIME "</p>"
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


/** OTA 维护页（自有根页处理 handleOtaRoot 引用） */
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
    "h+='<tr><td>固件版本</td><td>'+esc(s.fw_version||'—')+' · '+esc(s.build_time||'')+'</td></tr>';"
    "h+='<tr><td>WiFi</td><td>'+(s.wifi_status==3?ok(s.wifi_str):err(s.wifi_str))+'</td></tr>';"
    "h+='<tr><td>IP</td><td>'+esc(s.ip||'—')+'</td></tr>';h+='<tr><td>MAC</td><td style=font-size:11px>'+esc(s.mac||'—')+'</td></tr>';"
    "h+='<tr><td>模式</td><td>'+esc(s.mqtt_mode)+'</td></tr>';"
    "h+='<tr><td>MQTT</td><td>'+(s.mqtt_ready?ok('OK'):err('NO'))+'</td></tr>';"
    "h+='<tr><td>NTP</td><td>'+(s.clock_ready?ok('OK'):err('NO'))+'</td></tr>';"
    "h+='<tr><td>继电器</td><td>'+(s.flag_state==1?'开（蓝灯）':'关（红灯）')+'</td></tr>';"
    "h+='<tr><td>设备ID</td><td style=font-size:11px>'+esc(s.clientid||'—')+'</td></tr>';"
    "h+='<tr><td>运行固件</td><td>0x1000 起 · '+esc(s.sketch_bytes||'—')+' B</td></tr>';"
    "h+='<tr><td>OTA 暂存</td><td>'+esc(s.ota_offset||'—')+' 起 · '+esc(s.ota_space||'—')+' B 可用</td></tr>';"
    "h+='<tr><td>Flash</td><td>'+esc(s.flash_bytes||'—')+' B（文件系统 '+esc(s.fs_offset||'—')+' 起）</td></tr>';"
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


// 双保险清配网（网页与按键共用）：不依赖库 clearWiFi() 的静默删除——
// 删两个配置文件并回读校验，再擦 SDK 自己持久化的 WiFi 凭据，最后重启；
// 配网热点只在开机读不到配置时开启，故必须重启收尾。每步结果打串口，失效不再静默。
static void clearProvisioningAndRestart() {
  bool fsOk = LittleFS.begin();
  bool rmWifi = LittleFS.remove("/bgwificonfig/wifiset.txt");
  bool rmUmsg = LittleFS.remove("/bgwificonfig/umsg.txt");
  bool wifiGone = !LittleFS.exists("/bgwificonfig/wifiset.txt");
  bool umsgGone = !LittleFS.exists("/bgwificonfig/umsg.txt");
  WiFi.disconnect(true);
  bool sdkErased = ESP.eraseConfig();
  Serial.printf("clearProvisioning: fs=%d rmWifi=%d rmUmsg=%d wifiGone=%d umsgGone=%d sdkErased=%d, restarting\n",
                fsOk, rmWifi, rmUmsg, wifiGone, umsgGone, sdkErased);
  Serial.flush();
  delay(400);
  ESP.restart();
}

void handleOtaClearCfg() {
  OTAserver.sendHeader("Connection", "close");
  OTAserver.send(200, "text/html; charset=utf-8",
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>OK</title></head><body>"
    "<p>已清除，正在重启并进入配网…</p></body></html>");
  mqttclient.disconnect();
  clearPersistedFlagState();
  clearProvisioningAndRestart();
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
  json += ",\"mac\":\"" + WiFi.macAddress() + "\"";
  json += ",\"mqtt_ready\":" + String(mqttclient_state == 1 ? "true" : "false");
  json += ",\"clock_ready\":" + String(clockReady ? "true" : "false");
  json += ",\"flag_state\":" + String(flagState == HIGH ? "1" : "0");
  json += ",\"clientid\":\"" + jsonEscape(CLIENTID) + "\"";
  json += ",\"mqtt_mode\":\"" + String(customMqtt ? "custom" : "onenet") + "\"";
  json += ",\"fw_version\":\"" + String(FW_GIT_HASH) + "\"";
  json += ",\"build_time\":\"" + String(FW_BUILD_TIME) + "\"";
  // 分区布局（ESP8266+eboot 无 ESP32 式双执行分区：应用恒在 0x1000
  // 运行，OTA 镜像先落暂存窗、重启由 eboot 拷回）。暂存窗起点与
  // otaVerifyStaged 的地址公式同口径（当前固件扇区取整处）。
  {
    uint32_t sketchBytes = ESP.getSketchSize();
    uint32_t freeSpace = ESP.getFreeSketchSpace();
    uint32_t sketchRounded = (sketchBytes + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t fsStart = sketchRounded + freeSpace;
    json += ",\"sketch_bytes\":" + String(sketchBytes);
    json += ",\"flash_bytes\":" + String(ESP.getFlashChipRealSize());
    json += ",\"ota_space\":" + String(freeSpace);
    json += ",\"ota_offset\":\"0x" + String(sketchRounded, HEX) + "\"";
    json += ",\"fs_offset\":\"0x" + String(fsStart, HEX) + "\"";
  }
  json += ",\"reconnect_delay_ms\":" + String(mqttReconnectDelayMs);
  json += "}";
  OTAserver.sendHeader("Access-Control-Allow-Origin", "*");
  OTAserver.send(200, "application/json", json);
}

// ---- 自有 OTA 处理（替代库 OTAbegin 的注册与上传逻辑） ----
// 库的问题有二（2026-11-04 真机实锤）：
// 1) OTAbegin() 仅在调用瞬间 WiFi 已连通才注册 "/"、"/otaupdate" 并
//    begin 服务器，慢速开机整段运行期无维护页（恢复热点根页 404）；
// 2) 其上传回调把整块数据一次交给 Update.write，写 Flash 期间软看门狗
//    被饿死：上传约 8 秒 Soft WDT reset + Exception (4)，同源码 ELF 解码
//    堆栈落在 UpdaterClass::write → spi_flash_write_page_break。
// 这里在已配网（STA）时无条件注册并 begin，上传按片写、每片喂狗让出 CPU。
// 追加实测（同日晚）：切片版仍在约 7.6 秒 Soft WDT 崩溃，且小 15KB 的
// 作者 bin 对照实验同样崩、epc 落在 ROM Wait_SPI_Idle——单次 Flash 擦/写
// 操作本身阻塞超过软狗窗口，切片间喂狗救不了操作内部。故上传全程关
// 软狗（ESP.wdtDisable，硬件狗仍在），结束/中止恢复，并打进度日志。
static const uint32_t OTA_WRITE_SLICE = 512;
static bool otaUpdateOk = false;
static uint32_t otaLogSize = 0;
static uint32_t otaStartMs = 0;
// 回读校验（2026-11-04 20:20 坐实）：本机 Flash 写入存在静默坏块——
// OTA 写出的镜像曾带伤运行（Web 访问必崩循环），同 bin 经 esptool
// 写后校验串口重刷即愈。Updater 的 MD5 只算内存缓冲、从不回读，
// 故接收时自行累计 MD5，Update.end 后回读暂存区逐块比对，不一致
// 绝不置成功（不重启、旧固件完好，可直接重试或改串口刷）。
static MD5Builder otaRxMd5;
static uint8_t otaFileModeByte = 0;
static bool otaModeByteSaved = false;
// 自计已接收字节数。不能用 upload.totalSize 判断“首块”：WebServer 在
// WRITE 回调返回之后才把本块计入 totalSize（Parsing-impl.h），首块回调时
// totalSize=0、currentSize=2048，原判据 totalSize-currentSize==0 实际命中
// 的是第二块，存下的是文件第 2050 字节而非镜像头模式字节，回读比对时
// 把它写回 buf[2] => 必然 MD5 不符（2026-11-05 真机两台都 verify FAILED）。
static uint32_t otaRxBytes = 0;
static uint32_t otaMaxSpace = 0;

// 暂存区回读校验：暂存地址按内核 Updater::begin 的公式复算——
// begin 收到的是整窗尺寸 otaMaxSpace（非文件尺寸），暂存窗口末端
// 锚定在 FS 起点：起点 = FS 起点 − otaMaxSpace 扇区取整。首版误用
// 文件尺寸算地址、读偏 0xA000 必然误报，已按此修正（见测试契约）。
static bool otaVerifyStaged(uint32_t len) {
  uint32_t rounded = (otaMaxSpace + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
  uint32_t sketchRounded = (ESP.getSketchSize() + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
  uint32_t fsStart = sketchRounded + ESP.getFreeSketchSpace();
  uint32_t start = fsStart - rounded;
  MD5Builder rb;
  rb.begin();
  static uint8_t buf[1024];
  uint32_t done = 0;
  bool first = true;
  while (done < len) {
    uint32_t want = len - done;
    if (want > sizeof(buf)) {
      want = sizeof(buf);
    }
    uint32_t rd = (want + 3) & ~3u;  // flashRead 需 4 字节对齐，多读的不入 MD5
    if (!ESP.flashRead(start + done, buf, rd)) {
      Serial.printf("OTA verify: flashRead failed at %u\n", done);
      return false;
    }
    if (first) {
      // Updater 写首扇区时可能把镜像头 flash-mode 字节改成本机模式，
      // 回读比对前还原为文件原值（与 Updater 自身 MD5 的处理一致）。
      buf[2] = otaFileModeByte;
      first = false;
    }
    rb.add(buf, (uint16_t)want);
    done += want;
    yield();
  }
  rb.calculate();
  String got = rb.toString();
  bool match = (got == otaRxMd5.toString());
  Serial.printf("OTA verify: staged md5 %s (rx %s)\n",
                match ? "MATCH" : got.c_str(), otaRxMd5.toString().c_str());
  return match;
}

void handleOtaRoot() {
  OTAserver.sendHeader("Connection", "close");
  OTAserver.send(200, "text/html; charset=utf-8", String(FPSTR(otaIndexHtml)));
}

void handleOtaUpdateUpload() {
  HTTPUpload& upload = OTAserver.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaUpdateOk = false;
    Serial.printf("OTA上传文件名: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    otaMaxSpace = maxSketchSpace;
    ESP.wdtDisable();
    otaLogSize = 0;
    otaStartMs = millis();
    otaRxMd5.begin();
    otaModeByteSaved = false;
    otaRxBytes = 0;
    if (!Update.begin(otaMaxSpace)) {
      Serial.printf("OTA begin failed, err=%u\n", Update.getError());
    } else {
      Serial.printf("OTA begin ok: space=%u heap=%u t=%lu\n",
                    otaMaxSpace, ESP.getFreeHeap(), otaStartMs);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    {  // 接收侧 MD5 累计（按到达顺序、整块计入），并保存镜像头模式字节
      uint32_t m = upload.currentSize;
      uint8_t* q = upload.buf;
      if (!otaModeByteSaved && otaRxBytes == 0 && m > 2) {
        otaFileModeByte = q[2];
        otaModeByteSaved = true;
      }
      otaRxBytes += m;
      while (m > 0) {
        uint16_t step = m > 4096 ? 4096 : (uint16_t)m;
        otaRxMd5.add(q, step);
        q += step;
        m -= step;
      }
    }
    uint32_t remaining = upload.currentSize;
    uint8_t* p = upload.buf;
    while (remaining > 0) {
      uint32_t slice = remaining > OTA_WRITE_SLICE ? OTA_WRITE_SLICE : remaining;
      if (Update.write(p, slice) != slice) {
        break;  // 错误已锁存在 Update 中，完成处理回 FAIL，不重启可直接重试
      }
      p += slice;
      remaining -= slice;
      ESP.wdtFeed();
      yield();
    }
    if (upload.totalSize - otaLogSize >= 32 * 1024) {
      otaLogSize = upload.totalSize;
      Serial.printf("OTA prog %u t=%lu heap=%u\n",
                    upload.totalSize, millis(), ESP.getFreeHeap());
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    uint32_t endStart = millis();
    Serial.printf("OTA end start: total=%u t=%lu\n", upload.totalSize, endStart);
    if (Update.end(true)) {
      otaRxMd5.calculate();
      if (otaModeByteSaved && otaRxBytes == upload.totalSize &&
          otaVerifyStaged(upload.totalSize)) {
        otaUpdateOk = true;
        Serial.printf("OTA uploaded ok: %u bytes, end took %lu ms, restart to apply\n",
                      upload.totalSize, millis() - endStart);
      } else {
        Serial.println("OTA verify FAILED: staged image corrupt, NOT restarting (old firmware intact)");
      }
    } else {
      Serial.printf("OTA upload failed, err=%u, end took %lu ms\n",
                    Update.getError(), millis() - endStart);
    }
    ESP.wdtEnable(0);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    ESP.wdtEnable(0);
    Serial.println("OTA upload aborted");
  }
  yield();
}

void handleOtaUpdateDone() {
  ESP.wdtEnable(0);  // 兜底恢复软狗（正常路径已在 END/ABORTED 恢复，重复调用无害）
  OTAserver.sendHeader("Connection", "close");
  OTAserver.send(200, "text/plain", otaUpdateOk ? "OK" : "FAIL");
  if (otaUpdateOk) {
    Serial.flush();
    delay(200);
    ESP.restart();
  }
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
        clearProvisioningAndRestart();
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
  Serial.print("FW version: ");
  Serial.print(FW_GIT_HASH);
  Serial.print(" build: ");
  Serial.println(FW_BUILD_TIME);
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

  uint8_t macBytes[6];
  WiFi.macAddress(macBytes);
  char defaultIdBuf[16];
  ykFormatDeviceIdFromMac(defaultIdBuf, sizeof(defaultIdBuf), macBytes);
  String defaultDevId = String(defaultIdBuf);
  Serial.printf("MAC: %s, Default Device ID: %s\n", WiFi.macAddress().c_str(), defaultDevId.c_str());

  wifipw.setCompatibleMode(true);
  wifipw.setWiFiTimeOut(10);
  wifipw.useSpaceWiFi(true);
  wifipw.autoStart(true);
  String customHtml = String(FPSTR(provisioningHtml));
  customHtml.replace("%DEFAULT_DEV_ID%", defaultDevId);
  wifipw.setZDYhtml(customHtml);
  wifipw.setNumUMSG(6);
  wifipw.begin();
  // 自有 OTA 注册（替代库 OTAbegin，原因见 handleOtaUpdateUpload 上方注释）：
  // 已配网时 begin() 后为 STA 模式，此时无条件注册维护页与上传处理并启动
  // 服务器，与开机时 WiFi 连接快慢无关；配网 AP 模式不启动（避免与配网
  // 服务器端口冲突），配网提交后设备重启、届时走 STA 路径。loop 里仍由
  // wifipw.OTALoop() 驱动 handleClient（其内部同样以已配网为条件）。
  if (WiFi.getMode() == WIFI_STA) {
    OTAserver.on("/", HTTP_GET, handleOtaRoot);
    OTAserver.on("/otaupdate", HTTP_POST, handleOtaUpdateDone, handleOtaUpdateUpload);
    OTAserver.on("/otaclearcfg", HTTP_POST, handleOtaClearCfg);
    OTAserver.on("/status", HTTP_GET, handleStatus);
    OTAserver.begin();
    Serial.println("OTA server started (own handler)");
  }

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
      CLIENTID = defaultDevId;
    }
    Serial.printf("Mode: Custom MQTT -> %s:%d, id=%s\n", MQTT_HOST.c_str(), MQTT_PORT_N, CLIENTID.c_str());
  } else {
    customMqtt = false;
    MQTT_PORT_N = (p > 0 && p <= 65535) ? p : 8883;
    if (CLIENTID.length() > 0 && !ykDeviceIdValid(CLIENTID.c_str())) {
      Serial.printf("invalid device id '%s', fallback\n", CLIENTID.c_str());
    }
    if (CLIENTID.length() == 0 || !ykDeviceIdValid(CLIENTID.c_str())) {
      CLIENTID = defaultDevId;
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
