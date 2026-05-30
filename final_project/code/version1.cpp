#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "HX711.h"
#include "time.h"

// --- 引脚定义 ---
#define TFT_CS    1
#define TFT_DC    2
#define TFT_RST   3
#define TFT_BL    4
// TFT SCL=6, SDA=7 (硬件SPI默认)

#define HX711_DT  5
#define HX711_SCK 8
#define BEEP_PIN  10

// --- 配置参数 (请修改为你手机散发的热点信息) ---
const char* ssid = "YourPhoneHotspot"; // 手机热点名称
const char* password = "YourPassword"; // 手机热点密码

const char* ntpServer = "ntp.aliyun.com";
const long  gmtOffset_sec = 8 * 3600; // 北京时间东八区
const int   daylightOffset_sec = 0;

// --- 对象实例化 ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
HX711 scale;
WebServer server(80);

// --- 全局变量 ---
int SetTime = 30; // 默认30分钟
unsigned long totalMs;
unsigned long startTime = 0;
bool isReminding = false;
int drinkCount = 0;

// 重量检测变量
long currentWeight = 0;
long threshold = 50000; // 需要根据实际杯子重量微调这个阈值
bool isCupRemoved = false; 

// 时间记录
char lastDrinkTimeStr[20] = "未记录";
char currentTimeStr[20] = "获取中...";

// --- 网页前端代码 ---
String HtmlPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>智能水杯</title>";
  html += "<style>body{text-align:center;font-family:sans-serif;background:#f4f4f9;} button{padding:12px 20px;font-size:16px;border:none;border-radius:8px;background:#007BFF;color:white;margin:5px;cursor:pointer;} .danger{background:#dc3545;} .card{background:white;padding:20px;border-radius:12px;margin:20px auto;max-width:400px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}</style></head><body>";
  html += "<h2>?智能定时喝水控制系统</h2>";
  html += "<div class='card'>";
  html += "<p>当前设定间隔：<b>" + String(SetTime) + "</b> 分钟</p>";
  unsigned long now = millis();
  long remain = (totalMs - (now - startTime)) / 60000;
  if (remain < 0) remain = 0;
  html += "<p>距离下次喝水：<b>" + String(remain) + "</b> 分</p>";
  html += "<p>今日喝水次数：<b>" + String(drinkCount) + "</b></p>";
  html += "<p>上次喝水：<b>" + String(lastDrinkTimeStr) + "</b></p>";
  html += "</div>";
  html += "<a href='/reset'><button>手动重置计时</button></a><br><br>";
  html += "<a href='/close'><button class='danger'>关闭蜂鸣提醒</button></a>";
  html += "<h3>修改提醒时长</h3>";
  html += "<a href='/time15'><button>15分钟</button></a>";
  html += "<a href='/time30'><button>30分钟</button></a>";
  html += "<a href='/time60'><button>60分钟</button></a>";
  html += "</body></html>";
  return html;
}

// --- Web服务器路由 ---
void WebService() {
  server.on("/", []() { server.send(200, "text/html", HtmlPage()); });
  server.on("/reset", []() {
    startTime = millis();
    isReminding = false;
    digitalWrite(BEEP_PIN, LOW); // 低电平闭嘴
    server.send(200, "text/html", "<meta charset='UTF-8'>已重置计时 <br><a href='/'>返回控制面板</a>");
  });
  server.on("/close", []() {
    isReminding = false;
    digitalWrite(BEEP_PIN, LOW);
    server.send(200, "text/html", "<meta charset='UTF-8'>提醒已关闭 <br><a href='/'>返回控制面板</a>");
  });
  server.on("/time15", []() { SetTime = 15; totalMs = 15 * 60 * 1000; startTime = millis(); server.send(200, "text/html", "<meta charset='UTF-8'>已设置15分钟<br><a href='/'>返回</a>"); });
  server.on("/time30", []() { SetTime = 30; totalMs = 30 * 60 * 1000; startTime = millis(); server.send(200, "text/html", "<meta charset='UTF-8'>已设置30分钟<br><a href='/'>返回</a>"); });
  server.on("/time60", []() { SetTime = 60; totalMs = 60 * 60 * 1000; startTime = millis(); server.send(200, "text/html", "<meta charset='UTF-8'>已设置60分钟<br><a href='/'>返回</a>"); });
}

// --- 获取本地时间 ---
void updateLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  sprintf(currentTimeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void setup() {
  Serial.begin(115200);

  // 初始化引脚
  pinMode(BEEP_PIN, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(BEEP_PIN, LOW);  // 高电平触发，所以开机先写LOW让蜂鸣器闭嘴！
  digitalWrite(TFT_BL, HIGH);   // 点亮背光

  // 初始化彩屏
  tft.initR(INITR_BLACKTAB); // 1.8寸屏幕的经典初始化配置
  tft.setRotation(1);        // 旋转屏幕为横屏
  tft.fillScreen(ST77XX_BLACK);
  
  // 开机画面
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(30, 40);
  tft.print("Smart Cup");
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(20, 80);
  tft.print("Connecting WiFi...");

  // 连接WiFi获取时间
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  // 配置网络时间
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // 屏幕显示IP地址
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(5, 20);
  tft.print("WiFi Connected!");
  tft.setCursor(5, 40);
  tft.print("IP: ");
  tft.print(WiFi.localIP());
  tft.setCursor(5, 80);
  tft.print("Init HX711...");
  delay(3000);

  // 启动 Web 服务
  WebService();
  server.begin();

  // 初始化 HX711
  scale.begin(HX711_DT, HX711_SCK);
  // 这里可以加 scale.tare(); 如果你需要每次开机自动去皮

  totalMs = SetTime * 60 * 1000;
  startTime = millis();
}

void loop() {
  server.handleClient();
  updateLocalTime();

  unsigned long now = millis();
  unsigned long runTime = now - startTime;
  long leaveTime = (totalMs - runTime) / 1000; // 剩余秒数

  // 1. 处理称重传感器逻辑
  if (scale.is_ready()) {
    currentWeight = scale.read(); // 读取裸数据
    
    // 如果重量突然变轻，说明杯子被拿走了
    if (currentWeight < threshold) {
      isCupRemoved = true;
    }
    // 如果之前杯子被拿走了，现在重量又恢复了，说明刚刚喝完水放回去
    else if (currentWeight > threshold && isCupRemoved) {
      startTime = millis();
      isReminding = false;
      digitalWrite(BEEP_PIN, LOW); // 关掉蜂鸣器
      drinkCount++;
      isCupRemoved = false;
      
      // 更新上次喝水时间
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        sprintf(lastDrinkTimeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      }
    }
  }

  // 2. 检查是否超时需要报警
  if (leaveTime <= 0 && isReminding == false) {
    isReminding = true;
  }

  // 控制蜂鸣器 (高电平触发：响一下，停一下)
  if (isReminding) {
    if ((now / 500) % 2 == 0) digitalWrite(BEEP_PIN, HIGH); // 响
    else digitalWrite(BEEP_PIN, LOW);                       // 停
  } else {
    digitalWrite(BEEP_PIN, LOW);                            // 强制安静
  }

  // 3. UI 屏幕绘制 (防闪烁局部刷新)
  tft.fillScreen(ST77XX_BLACK); // 简易清屏，如果觉得闪烁可以优化为覆盖绘制
  
  // 顶部：北京时间
  tft.fillRect(0, 0, 160, 20, ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 6);
  tft.print("Time: ");
  tft.print(currentTimeStr);

  // 中间：状态或倒计时
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
    int m = leaveTime / 60;
    int s = leaveTime % 60;
    if(m<10) tft.print("0"); tft.print(m);
    tft.print(":");
    if(s<10) tft.print("0"); tft.print(s);
  }

  // 底部：数据统计
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(5, 95);
  tft.print("Last: "); tft.print(lastDrinkTimeStr);
  tft.setCursor(5, 110);
  tft.print("Count: "); tft.print(drinkCount);

  delay(200); // 降低刷新率，防止屏幕闪瞎眼
}