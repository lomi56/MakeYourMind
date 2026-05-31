#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "time.h"

// ================= 引脚定义 =================
// 严格按照你最新的物理接线修改
#define TFT_CS     1   // GPIO 1 片选
#define TFT_DC     2   // GPIO 2 数据/命令控制 ?? 已更新为2
#define TFT_RST    3   // GPIO 3 屏幕复位
// #define TFT_BL  4   // ?? BLK 已直连 3.3V，保留注释
#define TFT_SCLK   6   // GPIO 6 硬件 SPI 时钟 (SCK)
#define TFT_MOSI   7   // GPIO 7 硬件 SPI 数据 (MOSI/SDA)

#define SOIL_PIN   0   // GPIO 0 传感器 AO
#define BEEP_PIN   10  // GPIO 10 蜂鸣器控制
#define BEEP_ON    HIGH
#define BEEP_OFF   LOW

// ================= WiFi 配置 =================
const char* ssid = "Titanic"; 
const char* password = "20060426"; 

// 阿里云时间服务器 (获取北京时间)
const char* ntpServer = "ntp.aliyun.com";
const long  gmtOffset_sec = 8 * 3600; // 东八区
const int   daylightOffset_sec = 0;

// ================= 对象实例化 =================
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
WebServer server(80);

// ================= ?? 传感器校准参数 =================
int dryValue = 4095;  // 探头拿在空气中、擦干时的 AD 数值
int wetValue = 1500;  // 探头完全泡在水里的 AD 数值

// ================= 全局变量 =================
int currentMoisture = 0;   // 湿度百分比 (0-100)
int rawAnalogValue = 0;    // AD原始数据
int alarmThreshold = 30;   // 缺水报警线 (低于30%响)
bool isMuted = false;      // 是否被网页强制静音

char currentTimeStr[20] = "同步中...";
unsigned long lastScreenUpdate = 0; // 屏幕刷新计时器

// ================= 网页前端代码 =================
String HtmlPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='3'>"; 
  html += "<title>植物卫士 IoT 控制台</title>";
  html += "<style>body{text-align:center;font-family:'Segoe UI',sans-serif;background:#e8f5e9;} button{padding:12px 20px;font-size:16px;border:none;border-radius:8px;background:#4caf50;color:white;margin:5px;cursor:pointer;box-shadow: 0 2px 4px rgba(0,0,0,0.2); transition: 0.2s;} button:hover{opacity: 0.9;} .danger{background:#f44336;} .card{background:white;padding:30px;border-radius:15px;margin:20px auto;max-width:500px;box-shadow:0 10px 20px rgba(0,0,0,0.1);}</style></head><body>";
  
  html += "<h2 style='color:#2e7d32;'>? 智能植物卫士 IoT 控制台</h2>";
  html += "<div class='card'>";
  html += "<p style='font-size:18px; color:#555;'>北京时间: <b>" + String(currentTimeStr) + "</b></p>";
  html += "<hr style='border:0; border-top:1px solid #eee; margin:20px 0;'>";
  
  html += "<p>实时土壤湿度</p>";
  String color = currentMoisture < alarmThreshold ? "#f44336" : "#2e7d32";
  html += "<h1 style='color:" + color + ";font-size:72px;margin:10px 0;'>" + String(currentMoisture) + "%</h1>";
  
  if (currentMoisture < alarmThreshold) {
    html += "<p style='font-size:20px;'>状态：<b>?? 极度缺水，请立即浇水！</b></p>";
    if (isMuted) html += "<p style='color:#f44336;'>(警报已被手动静音)</p>";
    else html += "<p style='color:#f44336; animation: blink 1s infinite;'>(? 正在鸣笛报警 ?)</p>";
  } else {
    html += "<p style='font-size:20px;'>状态：<b>? 湿度完美，植物很健康</b></p>";
  }
  
  html += "<p style='color:#999;font-size:12px;margin-top:20px;'>当前报警线: 低于 " + String(alarmThreshold) + "% | 探头AD原始数值: " + String(rawAnalogValue) + "</p>";
  html += "</div>";
  
  // 操作按钮区
  if (currentMoisture < alarmThreshold && !isMuted) {
    html += "<a href='/mute'><button class='danger'>? 强制关闭蜂鸣器报警</button></a><br><br>";
  }
  html += "<a href='/set20'><button>设为缺水线: 20%</button></a>";
  html += "<a href='/set30'><button>设为缺水线: 30%</button></a>";
  html += "<a href='/set50'><button>设为缺水线: 50%</button></a>";
  
  html += "</body></html>";
  return html;
}

// ================= Web服务器路由 =================
void WebService() {
  server.on("/", []() { server.send(200, "text/html", HtmlPage()); });
  server.on("/mute", []() { 
    isMuted = true; 
    digitalWrite(BEEP_PIN, BEEP_OFF); 
    server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>已静音，正在返回首页...</p>"); 
  });
  server.on("/set20", []() { alarmThreshold = 20; server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>设置成功</p>"); });
  server.on("/set30", []() { alarmThreshold = 30; server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>设置成功</p>"); });
  server.on("/set50", []() { alarmThreshold = 50; server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>设置成功</p>"); });
}

// ================= 获取本地时间 =================
void updateLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  sprintf(currentTimeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void setup() {
  Serial.begin(115200);
  delay(100); // 稍微等待串口稳定
  
  // 初始化引脚，开机直接静音
  pinMode(BEEP_PIN, OUTPUT);
  digitalWrite(BEEP_PIN, BEEP_OFF);

  // 屏幕初始化 ?? 增加了多种备选 TAB，如果花屏请修改这里
  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(15, 30);
  tft.print("Plant Guard");
  
  // 连网过程
  WiFi.begin(ssid, password);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(15, 60);
  tft.print("Connecting WiFi...");
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  tft.fillScreen(ST77XX_BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(5, 20); tft.print("WiFi Connected!");
    tft.setCursor(5, 40); tft.print("IP: "); tft.print(WiFi.localIP());
    // 同步阿里云时间
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.print("\n[OK] WiFi Connected! IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(5, 20); tft.print("WiFi Failed!");
    tft.setCursor(5, 40); tft.print("Offline Mode");
    Serial.println("\n[WARN] WiFi Failed!");
  }
  delay(2000);
  tft.fillScreen(ST77XX_BLACK); // 彻底清屏，准备进入主循环 UI

  // 启动 Web 服务
  WebService();
  server.begin();
  
  Serial.println("====== System Ready ======");
}

void loop() {
  // 1. 处理网页请求并更新时间
  server.handleClient();
  updateLocalTime();

  // 2. 读取模拟数据并映射为百分比 (0-100)
  rawAnalogValue = analogRead(SOIL_PIN);
  currentMoisture = map(rawAnalogValue, dryValue, wetValue, 0, 100);
  currentMoisture = constrain(currentMoisture, 0, 100); 

  // 3. 智能报警逻辑
  // 如果浇水了，湿度恢复正常，则自动重置静音状态
  if (currentMoisture >= alarmThreshold) {
    isMuted = false;
  }

  // 决定是否响蜂鸣器
  bool shouldAlarm = (currentMoisture < alarmThreshold) && !isMuted;
  if (shouldAlarm) {
    if ((millis() / 300) % 2 == 0) digitalWrite(BEEP_PIN, BEEP_ON);
    else digitalWrite(BEEP_PIN, BEEP_OFF);
  } else {
    digitalWrite(BEEP_PIN, BEEP_OFF);
  }

  // 4. 定时刷新屏幕与串口 (缩短刷新时间到 500ms，因为屏幕局部刷新已经非常快)
  if (millis() - lastScreenUpdate > 500) {
    lastScreenUpdate = millis();
    
    // ====== 串口输出调试信息 ======
    Serial.printf("[%s] 湿度: %d%% | 原始AD: %d | 报警线: %d%%\n", 
                  currentTimeStr, currentMoisture, rawAnalogValue, alarmThreshold);

    // ====== 屏幕防闪烁优化绘制 ======
    // [顶部] 时间条
    tft.fillRect(0, 0, 160, 20, ST77XX_BLUE);
    tft.setTextColor(ST77XX_WHITE); 
    tft.setTextSize(1);
    tft.setCursor(5, 6);
    tft.print("Time: ");
    tft.print(currentTimeStr);

    // [中间] 状态文字 (使用黑色矩形仅覆盖这一行)
    tft.fillRect(0, 30, 160, 20, ST77XX_BLACK); 
    if (currentMoisture < alarmThreshold) {
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.setCursor(15, 35);
      tft.print("NEED WATER!");
    } else {
      tft.setTextColor(ST77XX_GREEN);
      tft.setTextSize(1);
      tft.setCursor(20, 35);
      tft.print("Moisture Level:");
    }
    
    // [中间] 湿度百分比数字 (带背景色的文本打印，自动覆盖旧数字！)
    char moistureStr[10];
    sprintf(moistureStr, "%-3d%%", currentMoisture); // 左对齐占位，防止位数变化留下残影
    
    if (currentMoisture < alarmThreshold) {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    } else {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    }
    tft.setTextSize(4);
    tft.setCursor(40, 60);
    tft.print(moistureStr);

    // [底部] 网络信息
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(5, 100);
    tft.print("IP: ");
    tft.print(WiFi.localIP().toString() + "    "); // 消除IP残影
    
    tft.setCursor(5, 115);
    tft.print("WiFi Web Control Ready");
  }
}