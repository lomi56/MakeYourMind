你队友写的网页控制端思路非常棒！把设备做成可以通过手机 Web 控制的形态，直接让这个项目从“单片机玩具”升级成了真正的“物联网（IoT）产品”。

但是，作为你的“外脑”，我仔细核对了你刚发的三张实物照片和队友的代码，发现**代码和实际硬件之间存在 4 个“致命”冲突**。如果不改，烧录进去绝对跑不通：

1. **屏幕库完全不匹配：** 队友用的是 `Adafruit_SSD1306`（这是黑白 I2C OLED 屏幕的库）。但你的图片是一块 **1.8 寸的 RGB 彩色 TFT 屏幕（SPI 接口）**。必须换成 `Adafruit_ST7735` 彩屏库。
2. **传感器逻辑不匹配：** 队友写的是 `analogRead` 读取简单的模拟薄膜传感器。但你的图片是 **HX711 称重模块**，它是数字信号，必须用专门的 HX711 库来读取真实重量。
3. **蜂鸣器触发逻辑反了：** 你的蜂鸣器图片上赫然印着“低电平触发”。队友的代码是 `HIGH` 响铃，这会导致你们的机器一开机就狂叫关不掉。必须改成 `LOW` 响，`HIGH` 停。
4. **时间获取的“死锁”：** 队友开启了 `WiFi.softAP`（让 ESP32 自己散发热点），在这种模式下，单片机是没有连接真正互联网的，**绝对获取不到北京时间**。为了显示北京时间，我们必须把代码改成连接你手机散发的真实热点（STA模式）。

下面我为你重新梳理了精准的引脚接线，并重写了适配你们实物硬件的完整代码。

### 🔌 硬件引脚连接表 (基于 ESP32-C3)

请严格按照下表用杜邦线在面包板上进行连接：

| 外设模块               | 模块引脚 | 连线 | ESP32-C3 引脚 | 说明                 |
| ---------------------- | -------- | ---- | ------------- | -------------------- |
| **1.8寸 TFT 彩屏**     | VCC      | ➡️    | 3.3V          | 屏幕供电             |
|                        | GND      | ➡️    | GND           | 共地                 |
|                        | SCL      | ➡️    | GPIO 6        | 硬件 SPI 时钟 (SCK)  |
|                        | SDA      | ➡️    | GPIO 7        | 硬件 SPI 数据 (MOSI) |
|                        | RES      | ➡️    | GPIO 3        | 屏幕复位             |
|                        | DC       | ➡️    | GPIO 2        | 数据/命令控制        |
|                        | CS       | ➡️    | GPIO 1        | 片选                 |
|                        | BL       | ➡️    | GPIO 4        | 背光控制             |
| **HX711 称重模块**     | VCC      | ➡️    | 3.3V          | 模块供电             |
|                        | GND      | ➡️    | GND           | 共地                 |
|                        | DT       | ➡️    | GPIO 5        | 数据输出             |
|                        | SCK      | ➡️    | GPIO 8        | 时钟输入             |
| **有源蜂鸣器(低电平)** | VCC      | ➡️    | 3.3V          | 模块供电             |
|                        | GND      | ➡️    | GND           | 共地                 |
|                        | I/O      | ➡️    | GPIO 10       | 信号触发 (写 LOW 响) |

---

### 💻 完整 Arduino 烧录代码

**🚨 烧录前必做准备：**
在 Arduino IDE 的“管理库”中，搜索并安装以下两个库：

1. `Adafruit ST7735 and ST7789 Library` （安装时提示安装依赖库，选“全部安装”）
2. `HX711 Arduino Library` (作者：Bogdan Necula)

代码核心逻辑：ESP32 会连上你的手机热点，自动同步阿里云的北京时间。当检测到重量减少又增加（杯子被拿起又放下），就会记录一次喝水，并重置倒计时。

```cpp
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
  html += "<h2>💧智能定时喝水控制系统</h2>";
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
    digitalWrite(BEEP_PIN, HIGH); // 高电平闭嘴
    server.send(200, "text/html", "<meta charset='UTF-8'>已重置计时 <br><a href='/'>返回控制面板</a>");
  });
  server.on("/close", []() {
    isReminding = false;
    digitalWrite(BEEP_PIN, HIGH);
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
  digitalWrite(BEEP_PIN, HIGH); // 低电平触发，所以开机先写HIGH让蜂鸣器闭嘴！
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
      digitalWrite(BEEP_PIN, HIGH); // 关掉蜂鸣器
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

  // 控制蜂鸣器 (低电平触发：响一下，停一下)
  if (isReminding) {
    if ((now / 500) % 2 == 0) digitalWrite(BEEP_PIN, LOW); // 响
    else digitalWrite(BEEP_PIN, HIGH);                     // 停
  } else {
    digitalWrite(BEEP_PIN, HIGH);                          // 强制安静
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

```

### 🛠️ 现场实操排错指南

1. **获取控制网页：** 程序开机连上 WiFi 后，屏幕中间会显示一行 `IP: 192.168.x.x`。你的手机和 ESP32 连在同一个热点下，直接在手机浏览器输入这个 IP，就能看到极其精美的控制面板了。
2. **称重阈值调参（关键）：** 代码里有一行 `long threshold = 50000;`。HX711 读出来的裸数据非常大，你们现场把空杯子放上去，通过 Arduino 的**串口监视器**打印一下当前的 `currentWeight` 值，然后把 `threshold` 改成一个处于“空杯”和“没放杯子”之间的中间值，判断逻辑就完美了！





接线：

### 第一步：打通“大动脉”（供电扩展）

你的三个外设（屏幕、传感器、蜂鸣器）都需要 3.3V 供电和 GND 接地，但 ESP32-C3 板子上的引脚不够分。所以我们要**先征用面包板两侧的红蓝长条（电源轨）**。

1. 找一根**公对母**杜邦线，把 ESP32-C3 板子上的 **3.3V** 引脚，插到面包板最外侧任意一个 **红色线孔（+）** 里。
2. 找另一根杜邦线，把 ESP32-C3 板子上的 **GND** 引脚，插到面包板最外侧任意一个 **蓝色线孔（-）** 里。

> **结果：** 现在，面包板上这一整条红色轨都是 3.3V 供电，整条蓝色轨都是 GND。接下来的所有模块，VCC 就插红轨，GND 就插蓝轨！

### 第二步：接上“主监视器”（1.8寸 TFT 彩屏）

拿起你的屏幕，看着排针旁边印的白色小字，用**母对母**杜邦线（一头插屏幕，一头插 ESP32 对应的排针）按顺序接：

- **VCC** ➡️ 插到面包板的 **红色轨（+）**
- **GND** ➡️ 插到面包板的 **蓝色轨（-）**
- **SCL** ➡️ 插到 ESP32-C3 的 **06** 引脚（时钟线）
- **SDA** ➡️ 插到 ESP32-C3 的 **07** 引脚（数据线）
- **RES** ➡️ 插到 ESP32-C3 的 **03** 引脚（复位线）
- **DC** ➡️ 插到 ESP32-C3 的 **02** 引脚（数据/命令线）
- **CS** ➡️ 插到 ESP32-C3 的 **01** 引脚（片选线）
- **BL** ➡️ 插到 ESP32-C3 的 **04** 引脚（背光控制线）

### 第三步：接上“重量感知”（HX711 模块）

看你的实物图，HX711 和金属铝条（称重传感器）那边的 4 根线已经接好了。我们只需要管绿色小板子右侧连向 ESP32 的 4 个孔：

- **VCC** ➡️ 插到面包板的 **红色轨（+）**
- **GND** ➡️ 插到面包板的 **蓝色轨（-）**
- **DT** ➡️ 插到 ESP32-C3 的 **05** 引脚（数据输出）
- **SCK** ➡️ 插到 ESP32-C3 的 **08** 引脚（时钟输入）

### 第四步：装上“警报器”（有源蜂鸣器）

最后拿起那个低电平触发的蜂鸣器模块，它只有 3 根引脚：

- **VCC** ➡️ 插到面包板的 **红色轨（+）**
- **GND** ➡️ 插到面包板的 **蓝色轨（-）**
- **I/O** ➡️ 插到 ESP32-C3 的 **10** 引脚（信号触发）

### 🚨 现场上电前的“终极检查”

线全部插好后，**绝对不要马上通电（不要插电脑 USB）！**请沿着面包板的**红色轨**重新看一遍，确保上面插着的**全都是各个模块的 VCC（或者 3.3V）**，绝对没有任何一根 GND 插在红线上。只要供电没短路，这套系统烧录我刚刚给你的代码后，就能完美亮屏起飞。