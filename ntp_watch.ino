#include <ESP8266WiFi.h>

#include <DHT.h>

#include <WiFiUdp.h>
#include <TimeLib.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define AP_SSID "" /* 接続するルーターのSSID */
#define AP_PASS "" /* 接続するルーターのパスワード */

#define SCREEN_WIDTH 128  /* OLEDの横のピクセル数 */
#define SCREEN_HEIGHT 64  /* OLEDの縦のピクセル数 */
#define OLED_RESET -1     /* OLEDのリセット端子(互換品で無い場合は-1) */

#define TIME_ZONE 9       /* タイムゾーンの設定(日本なら9) */
#define UPDATE_MIN_PRE 59 /* 1分経過直前の秒の値 */

Adafruit_SSD1306 display( SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET );
WiFiUDP Udp;
DHT dht( 14, DHT11 );

/* NTPサーバーのドメイン名(IP指定は非推奨) */
const char* timeServer = "ntp.nict.jp";
/* 曜日変換用の文字列 */
const char week_day[7][8] = { "(SUN)", "(MON)", "(TUE)", "(WED)", "(THU)", "(FRI)", "(SAT)" };
/* 温湿度データ */
float humi = 0.0;
float temp = 0.0;
/* 現在時刻(日本時間)を取得 */
time_t now_data = 0;

int     wifi_init();
void    set_display();
time_t getNtpTime();
void    sendNTPpacket(const char* address);

void setup()
{
  byte err_data = 0x00;

  Serial.begin( 9600 );
  
  /* 温湿度センサーの開始 */
  dht.begin();

  /* wi-fi通信の開始 */
  err_data |= wifi_init();

  /* UDP通信の開始 */
  if( !Udp.begin( 2390 ) )
  {
    /* 失敗時 */
    err_data |= 0x02;
  }

  /* OLED制御の開始 */
  if( !display.begin( SSD1306_EXTERNALVCC, 0x3C ) )
  {
    /* 失敗時 */
    err_data |= 0x04;
  }
  else
  {
    /* OLED表示のクリア */
    display.clearDisplay();
    display.display();
  }

  /* エラー判定処理 */
  if( err_data & 0x04 )
  {
    /* OLEDが動作していない場合 */
    Serial.println( "init error" );
    Serial.print( "0x" );
    Serial.println( err_data, HEX );

    /* 処理を開始しない */
    for(;;){ delay( 500 ); };
  }
  else if( 0 < err_data )
  {
    /* OLEDが動作している場合 */
    display.clearDisplay();          /* バッファのクリア */
    display.setTextSize( 2 );        /* 表示する文字サイズ */
    display.setTextColor( WHITE );   /* 表示する文字の色(固定?) */
    display.setCursor( 0, 0 );       /* 文字描画の開始位置 */
    display.println( "init error" ); /* 文字のデータをセット */
    display.print( "0x" );
    display.println( err_data, HEX );
    display.display();               /* OLEDへ描画 */

    /* 処理を開始しない */
    for(;;){ delay( 500 ); };
  }
  else
  {
    /* 成功時 */
    setSyncProvider( getNtpTime ); /* 補正に使用する関数設定 */
    setSyncInterval( 21600 );      /* 時刻補正を行う周期設定(秒) */
    
    /* 温湿度データの取得(初回) */
    humi = dht.readHumidity();
    temp = dht.readTemperature();
  }
}

void loop()
{
  set_display();

  delay( 1000 - ( millis() % 1000 ) ); /* 通常は最長300ms程度のため問題なし */
}

/* wi-fiの初期化関数 */
int wifi_init()
{
  if( !WiFi.begin( AP_SSID, AP_PASS ) )
  {
    /* 失敗時 */
    return 1;
  }

  /* wi-fiの接続待ち */
  for( int loop = 0; loop < 100; loop++ )
  {
    if( WL_CONNECTED == WiFi.status() )
    {
      /* 成功時 */
      break; /* 接続できたらループ終了 */
    }
    else if( WL_CONNECTED != WiFi.status() && 99 == loop )
    {
      /* 失敗時 */
      return 1;
      break;
    }
    
    delay( 100 );
  }

  /* 成功時 */
  return 0;
}

/* OLEDに描画する関数 */
void set_display()
{
  /* 現在時刻(日本時間)を取得 */
  time_t now_data = now();

  char day_data[12];
  char time_data[12];
  char sensor_data[12];

  /* 日付・時間のフォーマット形式 */
  const char* format_day = "%04d/%02d/%02d";
  const char* format_time = "%02d:%02d:%02d";
  const char* format_sensor = "%2.0f   %2.0f%%";

  sprintf( day_data, format_day, year( now_data ), month( now_data ), day( now_data ) );
  sprintf( time_data, format_time, hour( now_data ), minute( now_data ), second( now_data ) );
  sprintf( sensor_data, format_sensor, temp, humi );

  display.clearDisplay();        /* バッファのクリア */
  
  display.setTextColor( WHITE ); /* 表示する文字の色(固定?) */

  display.setTextSize( 1 );      /* 表示する文字サイズ */
  display.setCursor( 19, 0 );    /* 文字描画の開始位置 */
  display.println( day_data );   /* 日付のデータをセット */

  display.setCursor( 79, 0 );    /* 文字描画の開始位置 */
  display.println( week_day[weekday( now_data ) - 1] ); /* 曜日のデータをセット */

  display.setTextSize( 2 );      /* 表示する文字サイズ */
  display.setCursor( 16, 20 );   /* 文字描画の開始位置 */
  display.println( sensor_data );/* 温湿度のデータをセット */

  /* 温度単位をそれっぽく表示 */
  display.setCursor( 45, 20 );   /* 文字描画の開始位置 */
  display.println( "C" );
  display.setCursor( 36, 10 );   /* 文字描画の開始位置 */
  display.println( "." );

  display.setCursor( 16, 48 );   /* 文字描画の開始位置 */
  display.println( time_data );  /* 時間のデータをセット */
  
  display.display();             /* OLEDへ描画 */

  /* 1分ごとに温湿度更新 */
  if( UPDATE_MIN_PRE == second( now_data ) )
  {
    /* 温湿度データの取得 */
    humi = dht.readHumidity();
    temp = dht.readTemperature();
  }
}

/***

ここから下記コードを使用(一部変数の型などを変更しています)
ttps://github.com/PaulStoffregen/Time/blob/master/examples/TimeNTP/TimeNTP.ino

***/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      // Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + TIME_ZONE * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(const char* address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

/***

ここまで

***/
