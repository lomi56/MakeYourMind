#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "HX711.h"
#include "time.h"

// ================= 引脚重定义 (排雷修复版) =================
#define TFT_CS    1
#define TFT_DC    2
#define TFT_RST   3
#define TFT_BL    4
// 【白屏杀手】强制指定软件 SPI 引脚，杜绝底层库找不到硬件引脚的 Bug
#define TFT_MOSI  7  // 即你接的 SDA
#define TFT_SCLK  6  // 即你接的 SCL

// 【死机杀手】坚决避开 GPIO 8 (它是启动引脚，接错会导致无限重启乱叫)
#define HX711_DT  5
#define HX711_SCK 0  // ??请务必把杜邦线从 08 拔下来，插到 1000(GPIO 0) 上！

#define BEEP_PIN  10

// ================= 蜂鸣器开关宏 (乱叫终结者) =================
// 你的照片显示是低电平触发，所以开机必须先给 HIGH 让它安静。
#define BEEP_ON   LOW   // 触发发声
#define BEEP_OFF  HIGH  // 强制安静

// --- 配置参数 (请务必修改为你们现场手机的热点名称和密码) ---
const char* ssid = "YourPhoneHotspot"; 
const char* password = "YourPassword"; 

const char* ntpServer = "ntp.aliyun.com";
const long  gmtOffset_sec = 8 * 3600; 
const int   daylightOffset_sec = 0;

// 【核心修复】使用软件 SPI 构造函数，100% 解决白屏问题！
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
HX711 scale;
WebServer server(80);

// --- 全局变量 ---
unsigned long totalMs = 10 * 1000; // ??已默认改为 10秒 方便路演演示
unsigned long startTime = 0;
bool isReminding = false;
int drinkCount = 0;

long currentWeight = 0;
long threshold = 50000; // 如果现场杯子放上去没反应，请修改这个阈值
bool isCupRemoved = false; 

char lastDrinkTimeStr[20] = "未记录";
char currentTimeStr[20] = "获取中...";

// --- 网页前端代码 ---
String HtmlPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>智能水杯</title>";
  html += "<style>body{text-align:center;font-family:sans-serif;background:#f4f4f9;} button{padding:10px 15px;font-size:14px;border:none;border-radius:8px;background:#007BFF;color:white;margin:5px;cursor:pointer;} .danger{background:#dc3545;} .card{background:white;padding:20px;border-radius:12px;margin:20px auto;max-width:400px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}</style></head><body>";
  html += "<h2>?智能水杯控制台</h2>";
  html += "<div class='card'>";
  
  unsigned long now = millis();
  long remain = (totalMs - (now - startTime)) / 1000;
  if (remain < 0) remain = 0;
  
  html += "<p>距离下次喝水：<b style='color:red;font-size:24px;'>" + String(remain) + "</b> 秒</p>";
  html += "<p>今日喝水次数：<b>" + String(drinkCount) + "</b></p>";
  html += "<p>上次喝水：<b>" + String(lastDrinkTimeStr) + "</b></p>";
  html += "</div>";
  html += "<a href='/reset'><button>手动重置计时</button></a>";
  html += "<a href='/close'><button class='danger'>关闭蜂鸣提醒</button></a>";
  html += "<h3>修改演示模式</h3>";
  html += "<a href='/time10s'><button>10秒极速演示</button></a>";
  html += "<a href='/time30'><button>30分钟正常模式</button></a>";
  html += "</body></html>";
  return html;
}

// --- Web服务器路由 ---
void WebService() {
  server.on("/", []() { server.send(200, "text/html", HtmlPage()); });
  server.on("/reset", []() {
    startTime = millis();
    isReminding = false;
    digitalWrite(BEEP_PIN, BEEP_OFF); 
    server.send(200, "text/html", "<meta charset='UTF-8'>已重置 <br><a href='/'>返回</a>");
  });
  server.on("/close", []() {
    isReminding = false;
    digitalWrite(BEEP_PIN, BEEP_OFF); 
    server.send(200, "text/html", "<meta charset='UTF-8'>已静音 <br><a href='/'>返回</a>");
  });
  server.on("/time10s", []() { totalMs = 10 * 1000; startTime = millis(); server.send(200, "text/html", "<meta charset='UTF-8'>已设10秒<br><a href='/'>返回</a>"); });
  server.on("/time30", []() { totalMs = 30 * 60 * 1000; startTime = millis(); server.send(200, "text/html", "<meta charset='UTF-8'>已设30分钟<br><a href='/'>返回</a>"); });
}

// --- 获取本地时间 ---
void updateLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  sprintf(currentTimeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void setup() {
  Serial.begin(115200);

  pinMode(BEEP_PIN, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  
  // ??开机第一时间给高电平，让低电平触发的蜂鸣器闭嘴！
  digitalWrite(BEEP_PIN, BEEP_OFF);  
  digitalWrite(TFT_BL, HIGH);   

  // 【白屏终极抢救】如果你烧录后屏幕显示花屏或雪花，请把这行里的 BLACKTAB 替换为 GREENTAB 
  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(1);        
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(15, 30);
  tft.print("Starting...");

  WiFi.begin(ssid, password);
  int retry = 0;
  // 防卡死设计：如果10秒内连不上热点，直接跳过进入系统，不影响脱网演示
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(5, 20);
    tft.print("WiFi OK!");
    tft.setCursor(5, 50);
    tft.print(WiFi.localIP());
  } else {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(5, 20);
    tft.setTextColor(ST77XX_RED);
    tft.print("WiFi Failed");
  }
  delay(2000);

  WebService();
  server.begin();

  scale.begin(HX711_DT, HX711_SCK);
  startTime = millis();
}

void loop() {
  server.handleClient();
  updateLocalTime();

  unsigned long now = millis();
  unsigned long runTime = now - startTime;
  long leaveTime = (totalMs - runTime) / 1000; 

  if (scale.is_ready()) {
    currentWeight = scale.read(); 
    // 调试用：打开串口监视器，观察空杯和满杯的数值，以确定最佳 threshold 阈值
    Serial.print("Current Weight: ");
    Serial.println(currentWeight);
    
    if (currentWeight < threshold) {
      isCupRemoved = true;
    }
    else if (currentWeight > threshold && isCupRemoved) {
      startTime = millis();
      isReminding = false;
      digitalWrite(BEEP_PIN, BEEP_OFF); 
      drinkCount++;
      isCupRemoved = false;
      
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        sprintf(lastDrinkTimeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      }
    }
  }

  if (leaveTime <= 0 && isReminding == false) {
    isReminding = true;
  }

  if (isReminding) {
    // 报警状态：滴...滴...滴...
    if ((now / 500) % 2 == 0) digitalWrite(BEEP_PIN, BEEP_ON); 
    else digitalWrite(BEEP_PIN, BEEP_OFF);                       
  } else {
    digitalWrite(BEEP_PIN, BEEP_OFF);                            
  }

  tft.fillScreen(ST77XX_BLACK); 
  
  tft.fillRect(0, 0, 160, 20, ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 6);
  tft.print("Time: ");
  tft.print(currentTimeStr);

  if (isReminding) {
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.setCursor(15, 45);
    tft.print("Drink Water!");
  } else {
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.setCursor(5, 35);
    tft.print("Next drink in:");
    tft.setTextSize(3);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(25, 55);
    if(leaveTime < 0) leaveTime = 0;
    int m = leaveTime / 60;
    int s = leaveTime % 60;
    if(m<10) tft.print("0"); tft.print(m);
    tft.print(":");
    if(s<10) tft.print("0"); tft.print(s);
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(5, 95);
  tft.print("Last: "); tft.print(lastDrinkTimeStr);
  tft.setCursor(5, 110);
  tft.print("Count: "); tft.print(drinkCount);

  delay(200); 
}