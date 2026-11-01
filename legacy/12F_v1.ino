#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <PubSubClient.h>
 
#define eeshow
#define eeshowM 2
#define useLittleFS
#include <BGWiFiConfig.h>
 
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
             + String("<style> .input { border: 2px solid orange; padding: 10px; border-radius: 50px 20px; } .button { display: inline-block; padding: 15px 25px; font-size: 24px; cursor: pointer; text-align: center; text-decoration: none; outline: none; color: #fff; background-color: #4CAF50; border: none; border-radius: 15px; box-shadow: 0 9px #999; } .button:hover { background-color: #3e8e41 } .button:active { background-color: #3e8e41; box-shadow: 0 5px #666; transform: translateY(4px); } </style>")
              + String("</head><body><center><form action=\"htmlresult\" method=\"post\">")
              + String("<h4>WiFi名称:<input class=\"input\" type=\"text\" name=\"ssid\"></h4>")
              + String("<h4>WiFi密码:<input class=\"input\" type=\"text\" name=\"pwd\"></h4>")
              + String("<h4>输入设备ID:<input class=\"input\" type=\"text\" name=\"umsg1\" value=\"Z000005\"></h4>")
              + String("<h4>输入加密串:<input class=\"input\" type=\"text\" name=\"umsg2\" value=\"9cv%2Btxsv7sCr0GtOnR6VaQ%3D%3D\"></h4>")
              + String("<br>模式选择：<input type=\"radio\" name=\"mode\" value=\"1\" checked>mode:1<br>")
              + String("<input type=\"submit\" value=\"开始配网\"></form></body></html>");
 
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
 
//设备ID
String CLIENTID;
String SIGN_MQTT;
 
// 按钮 按下时间
int buttonState = LOW;
unsigned long buttonMillis;
 
// 开关状态 延时开关时间
int flagState = HIGH; 
unsigned long timeOut = 0; 
 
// MQTT消息更新间隔   客户端状态
unsigned long updateTime = 0; 
int mqttclient_state = 0;
 
// LED
unsigned long ledTime = 0; 
bool bLedState = LOW;
bool rLedState = HIGH;
 
// Set time via NTP, as required for x.509 validation
void setClock()
{
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}
 
 
void receivedCallback(char* topicname, uint8_t* payload, unsigned int length)
{
  String CMDREQUEST = SYS_STR + CLIENTID + "/cmd/request/";
 
  Serial.println("----------------------------------------------------------------------");
  Serial.println(length);
  Serial.print(" ）Message received: ");
  Serial.println(topicname);
  Serial.print("payload: ");
  char* msg = new char[length+1];
  for (int i = 0; i < length; i++)
  {
    msg[i] = (char)payload[i];
    Serial.print(msg[i]);
  }
  msg[length]='\0';
  Serial.println("\n----------------------------------------------------------------------");
  // 处理消息 strstr(topicname, CMDREQUEST); 返回指向 str1 中第一次出现 str2 的指针，如果 str2 不是 str1 的一部分，则返回空指针。
  char* res = strstr(topicname, CMDREQUEST.c_str());
  if(res != NULL)
  {
    // 解析消息
    int mIdHeight = 0;
    for (int index = 1; index < strlen(msg); index++) {
      mIdHeight = mIdHeight | (msg[index] & 0xff) << (index * 8);
    }
    timeOut = msg[0] & 0x1f | (mIdHeight >> 3);
    int flagStateTmp = msg[0] >> 5 & 0x7;
    if(flagStateTmp < 2){
      flagState = flagStateTmp;
    }
    Serial.print("+ msg:");
    Serial.println(msg);
    Serial.print("+ timeOut:");
    Serial.println(timeOut);
    Serial.print("+ flagState:");
    Serial.println(flagState);
    Serial.println("***************************");
 
    // 回复平台下发的指令，回复内容可自定义
    String topic = SYS_STR + CLIENTID + "/cmd/response/";
    char* cmdid = &topicname[CMDREQUEST.length()];
    //拼接
    topic.concat(cmdid);
    char result[11];
    // char * itoa(int value<被转换的整数>, char * string<转换后储存的字符数组>,int radix<转换进制数，如2,8,10,16 进制等>);
    itoa(flagState, result, 10);
    mqttclient.publish(topic.c_str(), result);
    // 消息包含退出
    if(strcmp(msg,"close") == 0) 
    {
      Serial.print("close ------------------- ");
    }
  }
}
 
void mqtt_update() {
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
      updateTime = millis() + 5000;
      // resetFunc();
    }
    return;
  }
  // 连接 mqtt
  Serial.println(wifipw.retWiFiSET());
  // 命令接收
  String CMDREQUESTJ = SYS_STR + CLIENTID + "/cmd/request/+";
  // 命令回复结果
  String CMDRESPONJJ = SYS_STR + CLIENTID + "/cmd/response/+/+";
  String PWD = "version=2018-10-31&res=products%2F538578%2Fdevices%2F" + CLIENTID + "&et=1924963199&method=md5&sign=" + SIGN_MQTT;
 
  // STEP3: Set CA Certification for TLS
  wifiClient.setTrustAnchors(&mqttcert);
  setClock();
  // if no check the CA Certification
  wifiClient.setInsecure();
  // STEP4: Set MQTT Parameters
  mqttclient.setClient(wifiClient);
  // configure the MQTT server with IPaddress and port, and the subscribe callback
  mqttclient.setServer(HOST, PORT);
  mqttclient.setCallback(receivedCallback);
  // STEP5: Connect to UCloud MQTT Broker
  if (mqttclient.connect(CLIENTID.c_str(), USER, PWD.c_str()))
  {
    mqttclient_state = 1;
    mqttclient.subscribe(CMDREQUESTJ.c_str(),1);
    mqttclient.subscribe(CMDRESPONJJ.c_str(),1);
    Serial.println("static connected");
    Serial.println(CMDREQUESTJ);
    Serial.println(CMDRESPONJJ);
    updateTime = millis() + 500;
  }
  else
  {
    Serial.printf("static connect failed, status code = %d\n", mqttclient.state());
    // Wait 5 seconds before retrying
    updateTime = millis() + 5000;
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
      if(downTime < 200){
 
      }
      else if(downTime < 3000)
      {
        Serial.println("buttonState HIGH < 3s");
        flagState = HIGH == flagState? LOW: HIGH;
      }
      else
      {
        Serial.println("buttonState HIGH > 3s");
        Serial.println("reset");
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
 
  // 初始化引脚
  pinMode(LED_B, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(FLAG, OUTPUT);
  pinMode(BUTTON, INPUT);
  //默认点亮红灯
  digitalWrite(LED_RED, HIGH);
	delay(100);
 
  // STEP2: Do SmartConifg and connecting to a WiFi network
	delay(10);
	Serial.println("Startup");
  Serial.println("readUMSG ----------------");
  for (int i = 1; i < 13; i++) {
    String msg = wifipw.readUMSG(i);
    if (msg != "NULL") {
      Serial.print("打印数据组[");
      Serial.print(i);
      Serial.print("]-->");
      Serial.println(msg);
    }
  }
  Serial.println("readUMSG END ----------------");
 
  Serial.begin(115200);
  wifipw.setCompatibleMode(true);
  wifipw.setConFailReset(2, 10,true);
  wifipw.useSpaceWiFi(true);
  wifipw.autoStart(true);
  wifipw.setZDYhtml(html);
  wifipw.setNumUMSG(2);
  //优化解决部分手机配网WiFi自动断连问题，传入true时启用兼容模式，配网IP将从192.168.22.22变为192.168.22.1。
  // wifipw.setCompatibleMode(true);      
  wifipw.begin();
  wifipw.setOTATimeOut(30);
  wifipw.OTAbegin();
 
  CLIENTID = wifipw.readUMSG(1);
  SIGN_MQTT = wifipw.readUMSG(2);
}
 
void loop()
{
  if(flagState == LOW){
    // 继电器常开
    digitalWrite(FLAG, LOW);
  }else if(flagState == HIGH){
    // 继电器常闭
    digitalWrite(FLAG, HIGH);
  }
 
  wifipw.Loop();
  wifipw.OTALoop();
  button_hok();
 
  
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