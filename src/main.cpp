#include <HTTPClient.h>
#include <M5Stack.h>

#include <math.h>

#include "pngle.h"

#include <driver/adc.h>
#include <rom/rtc.h>

#include "JsonListener.h"
#include "JsonStreamingParser.h"
#include "TrafficParser.h"

#define WIFI_SSID "XXXXXXXX"
#define WIFI_PASS "XXXXXXXX"

#define ZOOM 8
// 基本タイル
#define BASE_XTILE 228
#define BASE_YTILE 89
#define OFFSETX 0
#define OFFSETY -70
#define SCALE 1.0
#define TRAFFIC_REFRESH_MSEC 60 * 1000
#define MAP_REFRESH_MSEC 24 * 60 * 60 * 1000
#define MAX_EDGES 30

#define ARRAY_SIZE(x) sizeof(x) / sizeof(x[0])
String infos[] = {"C01"}; // 首都高

int i, xt, yt, _x, _y, base_xtile, base_ytile = 0;
int ix, iy = 0;
unsigned long lastRefreshTraffic = 0;
unsigned long lastRefreshMap = 0;

String generation;
String target = "";
String lastTarget = "";

uint8_t buf[2048];

TrafficListener listener;

int ax[MAX_EDGES], ay[MAX_EDGES], a[MAX_EDGES];
int edge = 0;

/* Define the structure to store the edges*/
struct edge {
  int x1, y1, x2, y2, flag;
};

void cls() {
  M5.Lcd.fillScreen(TFT_BLACK);

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
}

double g_scale = 1.0;
void pngle_on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w,
                   uint32_t h, uint8_t rgba[4]) {
  uint16_t color = (rgba[0] << 8 & 0xf800) | (rgba[1] << 3 & 0x07e0) |
                   (rgba[2] >> 3 & 0x001f);

  if (rgba[3]) {
    x = ceil((x + ix * 256 + OFFSETX) * g_scale);
    y = ceil((y + iy * 256 + OFFSETY) * g_scale);
    w = ceil(w * g_scale);
    h = ceil(h * g_scale);
    M5.Lcd.fillRect(x, y, w, h, color);
  }
}

int load_png(const char *url, double scale = SCALE) {
  HTTPClient http;

  http.begin(url);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP ERROR: %d\n", httpCode);
    http.end();
    return -1;
  }

  WiFiClient *stream = http.getStreamPtr();
  int length = http.getSize();

  pngle_t *pngle = pngle_new();
  pngle_set_draw_callback(pngle, pngle_on_draw);
  g_scale = scale;

  int remain = 0;
  while (http.connected() && (length > 0)) {
    size_t size = stream->available();
    if (!size) {
      delay(1);
      continue;
    }

    if (size > sizeof(buf) - remain) {
      size = sizeof(buf) - remain;
    }

    int len = stream->readBytes(buf + remain, size);
    if (len > 0) {
      length -= len;
      int fed = pngle_feed(pngle, buf, remain + len);
      if (fed < 0) {
        cls();
        Serial.printf("ERROR: %s\n", pngle_error(pngle));
        break;
      }

      remain = remain + len - fed;
      if (remain > 0)
        memmove(buf, buf + fed, remain);
    } else {
      delay(1);
    }
  }

  pngle_destroy(pngle);

  http.end();
  return 0;
}

int load_json(const char *url) {
  JsonStreamingParser parser;
  parser.setListener(&listener);

  HTTPClient http;

  http.begin(url);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP ERROR: %d\n", httpCode);
    http.end();
    return -1;
  }

  WiFiClient *stream = http.getStreamPtr();
  int length = http.getSize();

  int remain = 0;
  while (http.connected() && (length > 0)) {
    size_t size = stream->available();
    if (!size) {
      delay(1);
      continue;
    }

    if (size > sizeof(buf)) {
      size = sizeof(buf);
    }

    int len = stream->readBytes(buf, size);
    if (len > 0) {
      length -= len;
      for (int i = 0; i < len; i++) {
        parser.parse(buf[i]);
      }
    } else {
      delay(1);
    }
  }

  http.end();
  return 0;
}

String tile_to_url(int xtile, int ytile, int zoom_level) {
  // タイル座標を地図URLに変換
  return "https://www.jartic.or.jp/d/map/simple/" + generation +
         "/shutokou/EPSG_4326_0" + String(ZOOM - 1) + "/14_05/0" +
         String(xtile) + "_00" + String(ytile) + ".png";
}

void latlon_to_pos(double lat, double lon, int zoom_level) {
  // 緯度経度からタイル座標に変換（高速道路用）
  double xd = (lon / 180 + 1) * std::pow(2, zoom_level) / 2;
  xt = int(xd);
  _x = int((xd - xt) * 256);
  //  double yd = ((-log(tan((45 + lat / 2) * M_PI / 180)) + M_PI) * std::pow(2,
  //  zoom_level) / (2 * M_PI));
  double yd = (lat / 90 + 1) * std::pow(2, zoom_level) / 4;
  yt = int(yd);
  _y = int((yd - yt) * 256);
}

// https://bedeveloper.wordpress.com/a-c-program-to-fill-polygon-using-scan-line-fill-algorithm/
void fillpoly(int x[], int y[], int n, int color) {
  int gm, i, j, k;
  struct edge ed[MAX_EDGES], temped;
  float dx, dy, m[MAX_EDGES], x_int[MAX_EDGES], inter_x[MAX_EDGES];
  int ymax = -9999, ymin = 9999, yy, temp;

  for (i = 0; i < n; i++) {
    if (y[i] > ymax)
      ymax = y[i];
    if (y[i] < ymin)
      ymin = y[i];
    ed[i].x1 = x[i];
    ed[i].y1 = y[i];
  }
  /*store the edge information*/
  for (i = 0; i < n - 1; i++) {
    ed[i].x2 = ed[i + 1].x1;
    ed[i].y2 = ed[i + 1].y1;
    ed[i].flag = 0;
  }
  ed[i].x2 = ed[0].x1;
  ed[i].y2 = ed[0].y1;
  ed[i].flag = 0;
  /*Check for y1>y2, if not interchnge y1 and y2 */
  for (i = 0; i < n; i++) {
    if (ed[i].y1 < ed[i].y2) {
      temp = ed[i].x1;
      ed[i].x1 = ed[i].x2;
      ed[i].x2 = temp;
      temp = ed[i].y1;
      ed[i].y1 = ed[i].y2;
      ed[i].y2 = temp;
    }
  }
  /*Draw the polygon*/
  for (i = 0; i < n; i++) {
    M5.lcd.drawLine(ed[i].x1, ed[i].y1, ed[i].x2, ed[i].y2, color);
  }
  /*sorting of edges in the order of y1,y2,x1*/
  for (i = 0; i < n - 1; i++) {
    for (j = 0; j < n - 1; j++) {
      if (ed[j].y1 < ed[j + 1].y1) {
        temped = ed[j];
        ed[j] = ed[j + 1];
        ed[j + 1] = temped;
      }
      if (ed[j].y1 == ed[j + 1].y1) {
        if (ed[j].y2 < ed[j + 1].y2) {
          temped = ed[j];
          ed[j] = ed[j + 1];
          ed[j + 1] = temped;
        }
        if (ed[j].y2 == ed[j + 1].y2) {
          if (ed[j].x1 < ed[j + 1].x1) {
            temped = ed[j];
            ed[j] = ed[j + 1];
            ed[j + 1] = temped;
          }
        }
      }
    }
  }
  /*calculating 1/slope of each edge and storing top*/
  for (i = 0; i < n; i++) {
    dx = ed[i].x2 - ed[i].x1;
    dy = ed[i].y2 - ed[i].y1;
    if (dy == 0) {
      m[i] = 0;
    } else {
      m[i] = dx / dy;
    }
    inter_x[i] = ed[i].x1;
  }
  /*making the Actual edges*/
  yy = ymax;
  while (yy > ymin) {
    for (i = 0; i < n; i++) {
      if (yy > ed[i].y2 && yy <= ed[i].y1) {
        ed[i].flag = 1;
      } else
        ed[i].flag = 0;
    }
    j = 0;
    for (i = 0; i < n; i++) {
      if (ed[i].flag == 1) {
        if (yy == ed[i].y1) {
          x_int[j] == ed[i].x1;
          j++;
          if (ed[i - 1].y1 == yy && ed[i - 1].y1 < yy) {
            x_int[j] = ed[i].x1;
            j++;
          }
          if (ed[i + 1].y1 == yy && ed[i + 1].y1 < yy) {
            x_int[j] = ed[i].x1;
            j++;
          }
        } else {
          x_int[j] = inter_x[i] + (-m[i]);
          inter_x[i] = x_int[j];
          j++;
        }
      }
    }
    /*sorting the x intersaction*/
    for (i = 0; i < j; i++) {
      for (k = 0; k < j - 1; k++) {
        if (x_int[k] > x_int[k + 1]) {
          temp = (int)x_int[k];
          x_int[k] = x_int[k + 1];
          x_int[k + 1] = temp;
        }
      }
    }
    /*extracting pairs of values to draw lilnes*/
    for (i = 0; i < j; i = i + 2) {
      M5.lcd.drawLine((int)x_int[i], yy, (int)x_int[i + 1], yy, color);
    }
    yy--;
  }
}

void storeGeometry(double lat, double lon, int edges) {
  latlon_to_pos(lat, lon, ZOOM);
  ax[edges] = (_x + (xt - BASE_XTILE) * 256 + OFFSETX) * SCALE;
  ay[edges] = (256 * 2 - (_y + (yt - BASE_YTILE + 1) * 256) + OFFSETY) * SCALE;
}

void drawGeometry(String cs, int elements) {
  // 交通情報描画
  int color = 0x0000;
  if (cs == "01") {
    color = TFT_BLACK;
  } else if (cs == "401") {
    color = TFT_RED;
  } else if (cs == "402") {
    color = TFT_ORANGE;
  }
  fillpoly(ax, ay, elements, color);
}

int getGeneration() {
  // 最新地図世代
  int ret = load_json(
      "https://www.jartic.or.jp/d/map/simple/generation-current.json");
  if (ret < 0)
    return -1;
  generation = listener.target;
  Serial.println("generation: " + generation);
  return 0;
}

int getTarget() {
  // 最新情報の日時
  int ret = load_json("https://www.jartic.or.jp/d/traffic_info/r1/target.json");
  if (ret < 0)
    return -1;
  target = listener.target;
  Serial.println("target: " + target);
  return 0;
}

int getTrafficInfos(String route) {
  // 最新情報の日時
  int ret = load_json(("https://www.jartic.or.jp/d/traffic_info/r1/" + target +
                       "/s/301/" + route + ".json")
                          .c_str());
  if (ret < 0)
    return -1;
  return 0;
}

int refreshTraffic() {
  // 交通情報の更新
  int ret = getTarget();
  if (ret)
    return -1;
  for (int i = 0; i < ARRAY_SIZE(infos); i++) {
    ret += getTrafficInfos(infos[i]);
  }
  if (ret)
    return -1;
  return 0;
}

int drawMap() {
  // 地図の描画
  int ret = getGeneration();
  if (ret)
    return -1;
  cls();
  base_xtile = BASE_XTILE;
  base_ytile = BASE_YTILE;
  String url = "";
  for (iy = 0; iy < 2; iy++) {
    for (ix = 0; ix < 2; ix++) {
      url = tile_to_url(base_xtile + ix, base_ytile - iy, ZOOM);
      ret += load_png(url.c_str());
    }
  }
  if (ret)
    return -1;
  return 0;
}

void setup() {
  M5.begin();
  Serial.begin(115200);

  // for BtnA bug
  adc_power_acquire(); // ADC Power ON

  Serial.printf("Connecting WiFi.\n");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(1000);
  }

  Serial.printf("WiFi connected.\n");
}

void loop() {
  M5.update();

  if (lastRefreshTraffic == 0 ||
      millis() - lastRefreshTraffic > TRAFFIC_REFRESH_MSEC) {
    // 60秒毎に交通情報を更新をチェック
    Serial.println("heap: " + String(ESP.getFreeHeap()));
    if (getTarget()) {
      delay(1000);
    } else {
      if (lastTarget == target) {
        lastRefreshTraffic = millis();
      } else {
        // 更新あり
        if (drawMap()) {
          // failed
          delay(1000);
        } else if (refreshTraffic()) {
          // failed
          delay(1000);
        } else {
          lastRefreshTraffic = millis();
          lastTarget = target;
          M5.lcd.setTextSize(2);
          M5.lcd.setTextColor(TFT_BLACK);
          M5.lcd.setCursor(5, 5);
          M5.lcd.print(target.substring(0, 4) + "/" + target.substring(4, 6) +
                       "/" + target.substring(6, 8) + " " +
                       target.substring(8, 10) + ":" +
                       target.substring(10, 12));
        }
      }
    }
  }

  delay(10);
}
