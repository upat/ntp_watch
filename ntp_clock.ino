#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#include <DHT.h>

#include <WiFiUDP.h>
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
WiFiUDP UDP_NTP;
WiFiUDP UDP_RCV;
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
/* UDP通信用 */
char udp_buff[12] = "--%  --.-";
boolean udp_flag = false;
/* CGIアプリケーションのURL */
const char* cgi_url1 = "http://hoge/hoge1.py";
const char* cgi_url2 = "http://hoge/hoge2.py";

int     wifi_init();
void    set_display();
void    read_sensor();
boolean udp_rcv();
int     str_position( int str_length, int unit_length );
void    get_jmadata();
void    run_cgi();
time_t getNtpTime();
void    sendNTPpacket(const char* address);

void setup()
{
  byte err_data = 0x00;

  Serial.begin( 74880 );
  
  /* 温湿度センサーの開始 */
  dht.begin();

  /* wi-fi通信の開始 */
  err_data |= wifi_init();

  /* UDP通信の開始 */
  if( !UDP_NTP.begin( 2390 ) )
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
    get_jmadata();
    
    /* 成功時 */
    setSyncProvider( getNtpTime ); /* 補正に使用する関数設定 */
    setSyncInterval( 3600 );       /* 時刻補正を行う周期設定(秒) 後で調整 */
    
    /* 温湿度データの取得(初回) */
    read_sensor();
  }
}

void loop()
{
  /* NTPから取得した時刻が設定済み且つ時刻が更新された時 */
  if( timeNotSet != timeStatus() && now_data != now() )
  {
    now_data = now();
    set_display();

    if(
      8 < hour( now_data ) &&
      19 > hour( now_data ) &&
      1 < weekday( now_data ) &&
      7 > weekday( now_data )
      )
    // if( 19 == hour( now_data ) )
    {
      // Serial.println( "start deep-sleep" );
      // run_cgi();
      display.clearDisplay();
      display.display();
      // ESP.deepSleep( 30 * 1000 * 1000, WAKE_RF_DEFAULT );
      // ESP.deepSleep( ( unsigned long long )( 3600 * 9 * 1000 * 1000 ), WAKE_RF_DEFAULT );
      ESP.deepSleep( ( unsigned long )( ( 3600 + 240 ) * 1000 * 1000 ), WAKE_RF_DEFAULT );
      delay( 1000 );
    }

    /* 1分ごとに温湿度更新 */
    if( UPDATE_MIN_PRE == second( now_data ) )
    {
      read_sensor();
    }

    /* 毎時15分にUDPからデータ取得準備 */
    if( 15 == minute( now_data ) && 0 == second( now_data ) )
    {
      if( UDP_RCV.begin( 9000 ) )
      {
        sprintf( udp_buff, "--%%  --.-" );
        udp_flag = true;
      }
    }

    /* 毎時16分にUDP停止 */
    if( 16 == minute( now_data ) && 0 == second( now_data ) )
    {
      UDP_RCV.stop();
      udp_flag = false;
    }
  }

  /* 一定期間のみUDPからデータ受信を行う */
  if( udp_flag )
  {
    udp_flag = udp_rcv();
  }

  delay( 100 );
}

/* wi-fiの初期化関数 */
int wifi_init()
{
  /* 子機側に設定 */
  WiFi.mode( WIFI_STA );
  
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
      wifi_set_sleep_type( MODEM_SLEEP_T ); /* Modem-Sleep(未通信時にスリープさせる) */
      break; /* 接続できたらループ終了 */
    }
    delay( 100 );
  }

  if( WL_CONNECTED != WiFi.status() )
  {
    /* 失敗時 */
    return 1;
  }

  /* 成功時 */
  return 0;
}

/* OLEDに描画する関数 */
void set_display()
{
  /* 桁数調整用 */
  int hour_digit = 0;
  
  char day_data[12];
  char time_data[12];
  char sensor_data[12];

  /* 日付・時間のフォーマット形式 */
  const char* format_day = "%04d/%02d/%02d";
  const char* format_time = "%2d:%02d:%02d";
  const char* format_sensor = "%2.0f%% %2.0f";

  sprintf( day_data, format_day, year( now_data ), month( now_data ), day( now_data ) );
  sprintf( time_data, format_time, hour( now_data ), minute( now_data ), second( now_data ) );
  sprintf( sensor_data, format_sensor, humi, temp );

  /* 文字数取得 */
  String udp_str = udp_buff;
  String sensor_str = sensor_data;

  int udp_strlen = udp_str.length() * 6;
  int sensor_strlen = sensor_str.length() * 12;

  int udp_cursor = str_position( udp_strlen, 10 );
  int sensor_cursor = str_position( sensor_strlen, 18 );

  if( 10 > hour() )
  {
    hour_digit = 6;
  }
  else
  {
    hour_digit = 0;
  }

  display.clearDisplay();         /* バッファのクリア */
  
  display.setTextColor( WHITE );  /* 表示する文字の色(固定?) */

  /* 日付のデータをセット */
  display.setTextSize( 1 );
  display.setCursor( 19, 0 );
  display.println( day_data );

  /* 曜日のデータをセット */
  display.setCursor( 79, 0 );
  display.println( week_day[weekday( now_data ) - 1] );

  /* 気温のデータをセット */
  display.setCursor( udp_cursor, 10 );
  display.println( udp_buff );

  /* 温度単位をそれっぽく表示(文字サイズ:1) */
  display.setCursor( udp_cursor + udp_strlen + 4, 10 );
  display.println( "C" );
  display.setCursor( udp_cursor + udp_strlen - 1, 5 );
  display.println( "." );

  /* 温湿度のデータをセット */
  display.setTextSize( 2 );
  display.setCursor( sensor_cursor, 24 );
  display.println( sensor_data );

  /* 温度単位をそれっぽく表示(文字サイズ:2) */
  display.setCursor( sensor_cursor + sensor_strlen + 6, 24 );
  display.println( "C" );
  display.setCursor( sensor_cursor + sensor_strlen - 3, 14 );
  display.println( "." );

  /* 時間のデータをセット */
  display.setCursor( 16 - hour_digit, 48 );
  display.println( time_data );

  /* OLEDへ描画 */
  display.display();

  /* NTP取得時間の調整(一度だけ) */
  adjust_syncinterval();
}

/* 温湿度データ取得関数の呼び出し */
void read_sensor()
{
  humi = dht.readHumidity();
  temp = dht.readTemperature();
}

/* UDP受信処理関数 */
boolean udp_rcv()
{  
  /* UDPのデータ長 */
  int udp_len = 0;
  
  if( 0 < UDP_RCV.parsePacket() )
  {
    udp_len = UDP_RCV.read( udp_buff, 12 );

    if( 0 < udp_len )
    {
      Serial.println("Get Temperature Data");
      udp_buff[udp_len] = '\0';

      return false;
    }
  }

  return true;
}

/* 一度だけNTP取得時間の調整 */
void adjust_syncinterval()
{
  static boolean is_once = true; 
  
  if( is_once && 0 == minute() && 0 == second() )
  {
    setSyncInterval( 21600 ); /* 時刻補正を行う周期設定(秒) */
    
    is_once = false;
    Serial.println( "Adjust SyncInterval" );
  }
}

/* OLED上の文字列を中央寄せに調整する */
int str_position( int str_length, int unit_length )
{
  int half_width = 0;
  int half_strlen = 0;
  
  half_width = ( SCREEN_WIDTH - unit_length ) / 2;
  half_strlen = str_length / 2;

  return ( half_width - half_strlen );
}

/* サーバーから気象データ取得 */
void get_jmadata()
{
  HTTPClient client;

  String data = "";
  int http_get;

  /* タイムアウト時間の設定(15s) */
  client.setTimeout( 15000 );
  
  client.begin( cgi_url1 );
  http_get = client.GET();

  if( 0 > http_get )
  {
    Serial.println( client.errorToString( http_get ) );
  }
  else
  {
    data = client.getString();  
    data.toCharArray( udp_buff, data.length() );
  }

  client.end();
}

/* プログラム実行のみ */
void run_cgi()
{
  HTTPClient client;

  String data = "";
  int http_get;

  /* タイムアウト時間の設定(15s) */
  client.setTimeout( 15000 );
  
  client.begin( cgi_url2 );
  http_get = client.GET();

  if( 0 > http_get )
  {
    Serial.println( client.errorToString( http_get ) );
  }

  client.end();
}

/***

ここから下記コードを使用(一部変数の型などを変更しています)
ttps://github.com/PaulStoffregen/Time/blob/master/examples/TimeNTP/TimeNTP.ino

***/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (UDP_NTP.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = UDP_NTP.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      // Serial.println("Receive NTP Response");
      UDP_NTP.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
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
  UDP_NTP.beginPacket(address, 123); //NTP requests are to port 123
  UDP_NTP.write(packetBuffer, NTP_PACKET_SIZE);
  UDP_NTP.endPacket();
}

/***

ここまで

***/
