#include <Arduino.h>

#include <AnimatedGIF.h>
#include <HTTPClient.h>
#include <rpcWiFi.h>
#include <RTC_SAMD51.h>
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <NTPClient.h>
#include <SAMCrashMonitor.h>
#include <Time.h>

// ********************************************************************
//  Wio Terminalを使った強震モニタのウォッチャ
//
//  このプログラムは国立研究開発法人防災科学技術研究所の『強震モニタ』のデータを
//  定期的に確認し、予想震度情報の取得に成功した場合、Wio Terminalでビープ音を
//  鳴らし、LCDに予想震度情報、震源・P波・S波リアルタイム震度情報を一定時間表示
//  するプログラムです。
//
//  以下で配布されているM5Stack用プログラムをWio Terminalに移植したものです。
//  http://www.ria-lab.com/archives/3339
//
//  M5Stack版とできるだけ同じ動作になるようにしていますが、一部ハードウェアの
//  制限により動作が異なる部分があります。
//  最新のソースコードはこちらで公開しています。
//  https://github.com/weboo/wio-terminal-kmoni
//
//
//  NYSLライセンス
//  This software is distributed under the license of NYSL.
//  http://www.kmonos.net/nysl/
//
//  《謝辞》
//  このプログラムで使用している画像データは国立研究開発法人防災科学技術研究所が
//  提供しているサービス『強震モニタ』のデータを使用させていただいております。
//  貴重なデータを提供していただき国立研究開発法人防災科学技術研究所に感謝を申し上
//  げます。
//
//  ■国立研究開発法人防災科学技術研究所
//    https://www.bosai.go.jp/
//  ■強震モニタ
//    http://www.kmoni.bosai.go.jp/
//
// ********************************************************************

#define LCD_WIDTH  320            // LCDの横ドット数
#define LCD_HEIGHT 240            // LCDの縦ドット数
#define CHECK_INTERVAL 5          // チェック間隔(秒)
#define DISPLAY_COUNT  6          // LCD点灯時間(チェック回数)

unsigned long file_buffer_size;   // ダウンロードしたサイズ
unsigned char file_buffer[20000]; // ダウンロード用のバッファ

const char* ssid     = "*****";   // Wi-Fi接続のアクセスポイント名
const char* password = "*****";   // Wi-Fi接続のパスワード

RTC_SAMD51 rtc;
WiFiUDP udp;
NTPClient timeClient(udp, "ntp.jst.mfeed.ad.jp");
DateTime now;

char gif_dir_path[30];    // GIFが格納されているディレクトリ名
char gif_file_name[100];  // GIFファイル名
char gif_url[200];        // GIFが格納されているURL

static LGFX lcd;
static LGFX_Sprite mapimg(&lcd);  // 日本地図スプライト
AnimatedGIF gif;

bool displayOn;           // 点灯中フラグ
long displayOnCount = 0;  // 消灯までのカウント
uint32_t prevTime = 0;    // 処理タイマー用
bool isMuted = false;     // ミュートフラグ


void playTone(int tone, int duration)
{
  if (isMuted) return;

  for (long i = 0; i < duration * 1000L; i += tone * 2) {
    analogWrite(WIO_BUZZER, 128);
    delayMicroseconds(tone);
    analogWrite(WIO_BUZZER, 0);
    delayMicroseconds(tone);
  }
}


long doHttpGet(String url, uint8_t *p_buffer, unsigned long *p_len)
{
  HTTPClient http;
  http.setReuse(true);
  http.begin(url);

  int httpCode = http.GET();
  unsigned long index = 0;

  if (httpCode <= 0) {
    http.end();
    return -1;
  }
  if (httpCode != HTTP_CODE_OK) {
    delay(100);
    return -1;
  }

  WiFiClient *stream = http.getStreamPtr();

  int len = http.getSize();
  if (len != -1 && (unsigned long)len > *p_len) {
    return -1;
  }

  while (http.connected() && len > 0) {
    size_t size = stream->available();

    if (size > 0) {
      if ((index + size) > *p_len) {
        return -1;
      }
      int c = stream->readBytes(&p_buffer[index], size);

      index += c;
      if (len > 0) {
        len -= c;
      }
    }
    delay(1);
  }

  *p_len = index;
  return 0;
}


// 地図スプライトに描画
void GIFDrawMap(GIFDRAW *pDraw)
{
  uint8_t c, *s, cidx, usTemp[LCD_WIDTH];
  int x, y, lcd_x, lcd_y;

  y = pDraw->iY + pDraw->y; // current line
  lcd_y = (int32_t)(((double)y / pDraw->iHeight) * LCD_HEIGHT);

  s = pDraw->pPixels;
  for (x = 0; x < pDraw->iWidth; x++) {
    lcd_x = (int32_t)(((double)(x) / pDraw->iWidth) * LCD_WIDTH);

    // Convert gif colors to 16 colors palette
    // Need to rewrite better color quantization algorithm
    c = *s++;
    if (c < 8) {
      cidx = 1;
    } else if (c < 78) {
      cidx = 2;
    } else if (c < 113) {
      cidx = 6;
    } else if (c < 147) {
      cidx = 7;
    } else if (c < 168) {
      cidx = 3;
    } else if (c < 193) {
      cidx = 4;
    } else if (c < 223) {
      cidx = 5;
    } else {
      cidx = 8;
    }
    usTemp[lcd_x] = cidx;
  }

  mapimg.pushImage( 0, lcd_y, LCD_WIDTH, 1, usTemp );
  mapimg.fillRect( 0, 0, 220, 40, TFT_WHITE );
}


void GIFDraw(GIFDRAW *pDraw)
{
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[400];
  int x, y, lcd_x, lcd_y, iWidth;

  iWidth = pDraw->iWidth;
  usPalette = pDraw->pPalette;

  y = pDraw->iY + pDraw->y; // current line
  s = pDraw->pPixels;

  // Apply the new pixels to the main image
  uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
  int iCount;
  pEnd = s + iWidth;
  x = 0;
  iCount = 0; // count non-transparent pixels
  while(x < iWidth) {
    c = ucTransparent-1;
    d = usTemp;
    while (c != ucTransparent && s < pEnd) {
      c = *s++;
      if (c == ucTransparent) { // done, stop
        s--; // back up to treat it like transparent
      } else { // opaque
          *d++ = usPalette[c];
          iCount++;
      }
    } // while looking for opaque pixels
    if (iCount) { // any opaque pixels?
      lcd_x = (int32_t)(((double)(x) / pDraw->iWidth) * LCD_WIDTH);
      lcd_y = (int32_t)(((double)y / pDraw->iHeight) * LCD_HEIGHT);

      // actual size for labels
      if (x < 220 && y < 35) {
        lcd_y = y;
      } else if (lcd_x < 220 && lcd_y < 35) {
        return;
      }

      lcd.pushImage( pDraw->iX+lcd_x, lcd_y, iCount, 1, (lgfx::rgb565_t*)usTemp );

      x += iCount;
      iCount = 0;
    }
    // no, look for a run of transparent pixels
    c = ucTransparent;
    while (c == ucTransparent && s < pEnd) {
      c = *s++;
      if (c == ucTransparent)
          iCount++;
      else
          s--;
    }
    if (iCount) {
      x += iCount; // skip these
      iCount = 0;
    }
  }
} /* GIFDraw() */


// 強震モニタのデータをチェック
void checkKmoni() {

  // 現在日時を取得し対象日時を決定
  // サーバからデータを取得できないことがあるのでリアルタイム日時の1秒前を対象日時とする
  uint8_t sec = now.second();
  if (sec > 0) sec--;

  // 対象日時から強震モニタ上で取得したいデータのディレクトリ名とファイル名を決定
  sprintf(gif_dir_path, "%04d%02d%02d", now.year(), now.month(), now.day());
  sprintf(gif_file_name, "%s%02d%02d%02d", gif_dir_path, now.hour(), now.minute(), sec);

  // 日本地図を描画
  mapimg.pushSprite(0, 0);

  // 予想震度を取得
  sprintf(gif_url, "http://www.kmoni.bosai.go.jp/data/map_img/EstShindoImg/eew/%s/%s.eew.gif", gif_dir_path, gif_file_name);
  file_buffer_size = sizeof(file_buffer);
  if (doHttpGet(gif_url, file_buffer, &file_buffer_size) == 0) {
    // EEWをダウンロードできたときから指定回数分処理が回るまでLCDを点灯
    displayOnCount = 0;
    // LCDが消灯中の場合は点灯、BEEPを鳴らしていない場合はBEEPを鳴らす
    if (!displayOn) {
      lcd.setBrightness(255);
      displayOn = true;
      playTone(1200, 1000);
    }
    // 予想震度を描画
    gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDraw);
    while (gif.playFrame(true, NULL)) {}
    gif.close();
  }

  // LCDが点灯している状態の場合、震源とリアルタイム震度情報を取得を試みて
  // 取得できた場合は描画する
  if (displayOn) {
    // 震源・P波・S波
    sprintf(gif_url, "http://www.kmoni.bosai.go.jp/data/map_img/PSWaveImg/eew/%s/%s.eew.gif", gif_dir_path, gif_file_name);
    file_buffer_size = sizeof(file_buffer);
    if (doHttpGet(gif_url, file_buffer, &file_buffer_size) == 0) {
      // 震源・P波・S波を描画
      gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDraw);
      while (gif.playFrame(true, NULL)) {}
      gif.close();
    }

    // リアルタイム震度
    sprintf(gif_url, "http://www.kmoni.bosai.go.jp/data/map_img/RealTimeImg/jma_s/%s/%s.jma_s.gif", gif_dir_path, gif_file_name);
    file_buffer_size = sizeof(file_buffer);
    if (doHttpGet(gif_url, file_buffer, &file_buffer_size) == 0) {
      // リアルタイム震度を描画
      gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDraw);
      while (gif.playFrame(true, NULL)) {}
      gif.close();
    }
  }
}


void setup() {
  Serial.begin(115200);

  pinMode(WIO_BUZZER, OUTPUT);
  pinMode(WIO_KEY_A, INPUT_PULLUP);
  pinMode(WIO_KEY_B, INPUT_PULLUP);
  pinMode(WIO_KEY_C, INPUT_PULLUP);

  lcd.init();
  lcd.setRotation(1);
  lcd.setTextWrap(true, true);
  lcd.fillScreen(TFT_BLUE);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.println(F("[KYOSHIN]"));
  lcd.println();

  WiFi.mode(WIFI_STA);
  // WiFi.disconnect();

  lcd.print(F("WiFi: "));
  lcd.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(300);
      WiFi.begin(ssid, password);
      lcd.print(".");
  }
  lcd.print(F("\nIP Address: "));
  lcd.println (WiFi.localIP());

  // NTP時刻更新
  lcd.println(F("Sync NTP Server"));
  timeClient.begin();
  timeClient.setTimeOffset(9 * 60 * 60);  // sec
  timeClient.update();

  if (!rtc.begin()) {
      Serial.println(F("Couldn't find RTC"));
      while (1) delay(10); // stop operating
  }
  rtc.adjust(timeClient.getEpochTime());

  now = rtc.now();
  lcd.print(F("Time: "));
  lcd.println(now.timestamp(DateTime::TIMESTAMP_FULL));

  // サーバから日本地図を取得しスプライトとして描画
  lcd.println();
  lcd.println(F("Getting map..."));
  gif.begin(GIF_PALETTE_RGB565_LE);
  file_buffer_size = sizeof(file_buffer);

  if (doHttpGet(F("http://www.kmoni.bosai.go.jp/data/map_img/CommonImg/base_map_w.gif"), file_buffer, &file_buffer_size) != 0) {
      lcd.println(F("Map download failed"));
      return;
  }

  mapimg.setColorDepth(4);
  mapimg.createPalette();
  mapimg.setPaletteColor(0, TFT_TRANSPARENT);
  mapimg.setPaletteColor(1, TFT_BLACK);
  mapimg.setPaletteColor(2, 0x666666U);
  mapimg.setPaletteColor(3, 0x888888U);
  mapimg.setPaletteColor(4, 0x999999U);
  mapimg.setPaletteColor(5, 0xABABABU);
  mapimg.setPaletteColor(6, TFT_BLUE);
  mapimg.setPaletteColor(7, TFT_RED);
  mapimg.setPaletteColor(8, TFT_WHITE);
  mapimg.createSprite(LCD_WIDTH, LCD_HEIGHT);
  mapimg.setSwapBytes(false);
  mapimg.fillScreen(TFT_WHITE);

  gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDrawMap);
  while (gif.playFrame(true, NULL)) {}
  gif.close();

  // 画面を塗りつぶし
  lcd.fillScreen(TFT_WHITE);

  // 画面を点灯
  lcd.setBrightness(100);
  displayOn = true;

  playTone(440, 50);
  delay(50);

  SAMCrashMonitor::begin();
  SAMCrashMonitor::disableWatchdog();
  SAMCrashMonitor::dump();
  SAMCrashMonitor::enableWatchdog(10 * 1000);

  // LCD描画開始
  lcd.startWrite();
}


void loop() {
  SAMCrashMonitor::iAmAlive();

  now = rtc.now();
  uint32_t curTime = now.unixtime();

  // ボタンの状態をチェック
  if (digitalRead(WIO_KEY_C) == LOW) {
    // ボタンC -> LCDを点灯
    lcd.setBrightness(255);
    displayOn = true;
    displayOnCount = 0;
    playTone(1200, 50);
    delay(20);
    lcd.fillRoundRect(80, 100, 160, 36, 5, TFT_BLACK);
    lcd.setTextColor(TFT_WHITE);
    lcd.setCursor(100, 110);
    lcd.print("LOADING...");
  }
  else if (digitalRead(WIO_KEY_B) == LOW) {
    // ボタンB -> ミュート
    lcd.fillRoundRect(80, 100, 160, 36, 5, TFT_BLACK);
    lcd.setTextColor(TFT_WHITE);
    if (isMuted) {
      isMuted = false;
      lcd.setCursor(120, 110);
      lcd.print("MUTE OFF");
    } else {
      isMuted = true;
      lcd.setCursor(140, 110);
      lcd.print("MUTE");
    }
  }
  else if (digitalRead(WIO_KEY_A) == LOW) {
    // ボタンA -> LCDを消灯
    lcd.setBrightness(0);
    displayOn = false;
    displayOnCount = 0;
    playTone(1200, 50);
    delay(20);
  }

  // 強震モニタをチェックする
  if ((curTime - prevTime) >= CHECK_INTERVAL) {
    checkKmoni();
    displayOnCount++;
    prevTime = curTime;
  }

  // LCD消灯までカウントダウン
  if (displayOnCount >= DISPLAY_COUNT) {
    if (displayOn) {
      // LCDを消灯
      lcd.setBrightness(0);
      displayOn = false;
    }
  }

  // 次の処理まで待機
  delay(200);
}
