//#include <Arduino.h>
//Initialize Audio first to prevent bootloop
#include "Audio.h"
//#include "driver/i2s.h"
Audio audio(true, I2S_DAC_CHANNEL_LEFT_EN);



#include <ArduinoJson.h>
#include "driver/ledc.h"
#define LEDC_CHANNEL_0 LEDC_CHANNEL_0
#include <lvgl.h>
#include <Preferences.h>
#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include <SPIFFS.h>
//radio Stations
struct RadioStation {
  String name;
  String url;
};
AsyncWebServer server(80);

#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

// The CYD touch uses some non default
// SPI pins

#define orientation PORTRAIT

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO 21  // Backlight PIN
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_12_BIT  // Set duty resolution to 12 bits
#define LEDC_FREQUENCY 5000              // Frequency in Hertz

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33
SPIClass touchscreenSpi = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
uint16_t touchScreenMinimumX = 200, touchScreenMaximumX = 3700, touchScreenMinimumY = 240, touchScreenMaximumY = 3800;
//Poti Pin for Volume
#define POTI_PIN 35
// variable for storing the potentiometer value and volume
static int potValue = 0;
static int volume = 90;
/*Set to your screen resolution*/
#if orientation == PORTRAIT
#define TFT_HOR_RES 240
#define TFT_VER_RES 320
#elif orientation == LANDSCAPE
#define TFT_HOR_RES 320
#define TFT_VER_RES 240
#endif

static int seconds = 0;
/*LVGL draw into this buffer, 1/10 screen size usually works well. The size is in bytes*/
#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
//#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES * 2)

#if LV_USE_LOG != 0
void my_print(lv_log_level_t level, const char *buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}
#endif

/* LVGL calls it when a rendered image needs to copied to the display*/
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  /*Call it to tell LVGL you are ready*/
  lv_disp_flush_ready(disp);
}
void ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax = 255) {
  uint32_t duty = (4095 * min(value, valueMax)) / valueMax;  // 4095 is max duty cycle for 12-bit resolution
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}
/*Read the touchpad*/

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touchscreen.touched()) {
    seconds = 0;
    ledcAnalogWrite(LEDC_CHANNEL_0, 255);
    TS_Point p = touchscreen.getPoint();
    //Some very basic auto calibration so it doesn't go out of range
    if (p.x < touchScreenMinimumX) touchScreenMinimumX = p.x;
    if (p.x > touchScreenMaximumX) touchScreenMaximumX = p.x;
    if (p.y < touchScreenMinimumY) touchScreenMinimumY = p.y;
    if (p.y > touchScreenMaximumY) touchScreenMaximumY = p.y;
    //Map this to the pixel position
    data->point.x = map(p.x, touchScreenMinimumX, touchScreenMaximumX, 1, TFT_HOR_RES);
    data->point.y = map(p.y, touchScreenMinimumY, touchScreenMaximumY, 1, TFT_VER_RES);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}



lv_indev_t *indev;      //Touchscreen input device
uint8_t *draw_buf;      //draw_buf is allocated on heap otherwise the static area is too big on ESP32 at compile
uint32_t lastTick = 0;  //Used to track the tick timer


int selectedNetIndex = -1;  // Global variable to store the selected network index
Preferences preferences;

#if LV_USE_TABVIEW

lv_obj_t *label;
static lv_obj_t *radiolist;
static lv_obj_t *wifilist;
//Bluetooth Toggle button
lv_obj_t *label_btn_toggle;
lv_obj_t *bt_explain_text;
lv_obj_t *tab1;
lv_obj_t *tab2;

//Keyboard
lv_obj_t *kb;
lv_obj_t *keyboard_text_area;

//Status variable
lv_obj_t *wifiStatusLabel;  // Global variable for the WiFi status label
/*

const RadioStation stations[] = {
  { "Disco Ball 70's-80's L.A.", "http://sc8.1.fm:8100/" },
  { "Radio Paradise", "http://stream.radioparadise.com/mp3-192" },
  { "Ego FM", "http://www.egofm.de/stream/128kb.m3u" }

};*/

RadioStation *stations = nullptr;  // Global pointer for radio stations array
size_t stationCount = 0;           // Number of stations




static void connect_to_wifi(const char *ssid = nullptr, bool useKeyboardPassword = false) {
  lv_label_set_text(wifiStatusLabel, "Connecting to WiFi...");
  String storedSSID = "";
  String storedPassword = "";

  preferences.begin("wifi", true);
  storedSSID = preferences.getString("ssid", "");
  storedPassword = preferences.getString("password", "");
  preferences.end();

  if (ssid != nullptr) {
    Serial.print(F("Connecting to new wifi: "));
    Serial.println(ssid);

    WiFi.begin(ssid, lv_textarea_get_text(keyboard_text_area));

    int counter = 0;
    while (WiFi.status() != WL_CONNECTED && counter < 20) {
      delay(500);
      Serial.print(F("."));
      counter++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("\nConnected new WiFi"));
      //Serial.print(F("Local ESP32 IP: "));
      Serial.println(WiFi.localIP());
      String connectedMsg = String(WiFi.localIP().toString());
      connectedMsg += "-";
      connectedMsg += ssid;
      lv_label_set_text(wifiStatusLabel, connectedMsg.c_str());
setupServer();

      if (useKeyboardPassword) {
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("password", lv_textarea_get_text(keyboard_text_area));
        preferences.end();
      }
    } else {
      Serial.println(F("\n Wifi failed"));
      lv_label_set_text(wifiStatusLabel, "Not connected");
    }

    if (useKeyboardPassword) {
      lv_obj_delete(keyboard_text_area);
      lv_obj_delete(kb);
    }
  } else {
    if (storedSSID == "") {
      lv_label_set_text(wifiStatusLabel, "Not connected");
      return;
    }

    Serial.print(F("Connecting to stored SSID: "));
    Serial.println(storedSSID);
    Serial.print(F("Using PW: ")); 
    Serial.println(storedPassword);

    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

    int counter = 0;
    while (WiFi.status() != WL_CONNECTED && counter < 20) {
      delay(500);
      Serial.print(F("."));
      counter++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("\nConnected to the WiFi network"));
      Serial.print(F("Local ESP32 IP: "));
      Serial.println(WiFi.localIP());

      
      String connectedMsg = String(WiFi.localIP().toString());
      connectedMsg += "-";
      connectedMsg += storedSSID;
      lv_label_set_text(wifiStatusLabel, connectedMsg.c_str());
   
      setupServer();
    } else {
      Serial.println(F("\nFailed to connect"));
      lv_label_set_text(wifiStatusLabel, "Not connected");
    }
  }
}

static void keyboard_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CANCEL) {
    Serial.println(F("Closing Keyboard"));
    lv_obj_delete(keyboard_text_area);
    lv_obj_delete(kb);
  } else if (code == LV_EVENT_READY) {
    Serial.println(F("Password entered"));
    char *ssid = (char *)lv_event_get_user_data(e);  // Correctly cast the user data to char*
    Serial.println(ssid);
    connect_to_wifi(ssid, true);
  }
}

static void create_password_keyboard(char *ssid) {
  kb = lv_keyboard_create(lv_screen_active());
  lv_obj_add_event_cb(kb, keyboard_event_handler, LV_EVENT_ALL, ssid);  // Passing SSID to event handler
  keyboard_text_area = lv_textarea_create(lv_screen_active());
  lv_obj_align(keyboard_text_area, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_textarea_set_placeholder_text(keyboard_text_area, "WiFi Password...");
  lv_obj_set_size(keyboard_text_area, lv_pct(90), 80);
  lv_keyboard_set_textarea(kb, keyboard_text_area);
}

static void ssid_select_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);  // Explicitly cast to lv_obj_t*
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *ssid = lv_label_get_text(label);

    Serial.println(F("Opening Keyboard"));
    create_password_keyboard(const_cast<char *>(ssid));  // Note on const_cast below
  }
}

static void create_wifi_list(int numWifis) {
  wifilist = lv_list_create(tab2);
  lv_obj_align(wifilist, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_size(wifilist, LV_PCT(100), LV_PCT(100));



  for (int thisNet = 0; thisNet < numWifis; thisNet++) {
    int *netIndex = new int(thisNet);  // Allocate memory for network index
    lv_obj_t *btn_wifi;
    btn_wifi = lv_button_create(wifilist);
    lv_obj_set_width(btn_wifi, lv_pct(100));
    lv_obj_add_event_cb(btn_wifi, ssid_select_event_handler, LV_EVENT_CLICKED, netIndex);
    lv_obj_t *lab_wifi = lv_label_create(btn_wifi);
    lv_label_set_text_fmt(lab_wifi, WiFi.SSID(thisNet).c_str(), 0);
  }
}
static int listNetworks() {
  // scan for nearby networks:
  Serial.println(F("** Scan Networks **"));
  int numSsid = WiFi.scanNetworks();
  if (numSsid == -1) {
    Serial.println(F("Couldn't get a wifi connection"));
    while (true)
      ;
  }

  // print the list of networks seen:
  Serial.print(F("number of available networks:"));
  Serial.println(numSsid);

  // print the network number and name for each network found:
  for (int thisNet = 0; thisNet < numSsid; thisNet++) {
    Serial.print(thisNet);
    Serial.print(F(") "));
    Serial.print(WiFi.SSID(thisNet));
    Serial.print(F("\tSignal: "));
    Serial.println(WiFi.RSSI(thisNet));
  }
  return numSsid;
}
static void refresh_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_CLICKED) {
    Serial.println(F("Refreshing networks"));
    int wifiStationsAmount = listNetworks();
    create_wifi_list(wifiStationsAmount);
  }
}
static void connect_to_radio(const char *url) {
  bool succeeded = false;
  audio.forceMono(true);
  audio.setVolume(volume);

  if (WiFi.status() == WL_CONNECTED) {
    int counter = 0;
    Serial.println(F("Connecting to radio.."));
    do {
      // Connect to 'FM - Disco Ball 70's-80's Los Angeles'
      succeeded = audio.connecttohost(url);
      delay(500);
      Serial.print(F("."));
      counter++;
    } while (!succeeded && counter < 10);
  }
  if (!succeeded) {
    Serial.println(F("Radio Connect Failed"));
  }
}


static void radio_play_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_CLICKED) {
    const char *url = (const char *)lv_event_get_user_data(e);  // Cast back to const char*
    Serial.println(F("Playing radio url:"));
    Serial.println(url);
    connect_to_radio(url);  // Pass const char* to connect_to_radio
  }
}





static void create_tabs(void) {
  // Create the WiFi status label above the tabs


  //Create a Tab view object
  lv_obj_t *tabview;


  tabview = lv_tabview_create(lv_screen_active());  // Adjust the margin as needed
  lv_obj_set_pos(tabview, 0, 20);                   // Set the new y position

  //Add 3 tabs (the tabs are page (lv_page) and can be scrolled
  tab1 = lv_tabview_add_tab(tabview, "Radio");
  tab2 = lv_tabview_add_tab(tabview, "Wifi");

  //TAB1 -  Radio


  radiolist = lv_list_create(tab1);
  lv_obj_set_size(radiolist, LV_PCT(100), LV_PCT(100));
  for (size_t i = 0; i < stationCount; i++) {
    // Access the name and url members of each RadioStation struct
    const char *name = stations[i].name.c_str();  // Convert String to const char*
    const char *url = stations[i].url.c_str();    // Convert String to const char*

    //lv_obj_set_size(radiolist, 290, 200);
    lv_obj_t *btn;
    btn = lv_button_create(radiolist);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text_fmt(lab, name, 0);  // Pass const char* to lv_label_set_text_fmt
    lv_obj_add_event_cb(btn, radio_play_event_handler, LV_EVENT_CLICKED, (void *)url);
  }






  //Tab 2 - Settings


  //Refresh Button
  lv_obj_t *refresh_btn;
  refresh_btn = lv_button_create(tab2);
  lv_obj_set_size(refresh_btn, LV_PCT(100), 25);
  lv_obj_align(refresh_btn, LV_ALIGN_TOP_MID, 0, -10);
  lv_obj_add_event_cb(refresh_btn, refresh_event_handler, LV_EVENT_ALL, NULL);
  lv_obj_remove_flag(refresh_btn, LV_OBJ_FLAG_PRESS_LOCK);

  lv_obj_t *label_btn = lv_label_create(refresh_btn);
  lv_label_set_text(label_btn, "Refresh");
  lv_obj_center(label_btn);


  // WIFI LIST SSIDs
  int wifiStationsAmount = listNetworks();
  create_wifi_list(wifiStationsAmount);

  lv_tabview_set_active(tabview, 0, LV_ANIM_OFF);
}
#endif

static void clearPreferences() {
  preferences.begin("wifi", false);  // Open the Preferences with the namespace "wifi". RW-mode false.
  preferences.clear();               // This will remove all keys under the "wifi" namespace.
  preferences.end();                 // Close the Preferences
  Serial.println(F("Preferences cleared."));
}


static uint32_t my_tick_get_cb(void) {
  return millis();
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Internet Radio"));
  stations = loadStations(stationCount);


  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  //Initialise the touchscreen
  touchscreenSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS); /* Start second SPI bus for touchscreen */
  touchscreen.begin(touchscreenSpi);                                         /* Touchscreen init */
#if orientation == LANDSCAPE
  touchscreen.setRotation(3); /* Inverted landscape orientation to match screen */
#endif
#if orientation == PORTRAIT
  touchscreen.setRotation(2); /* Inverted landscape orientation to match screen */
#endif

  //Initialise LVGL
  lv_init();
  lv_tick_set_cb(my_tick_get_cb);
  draw_buf = new uint8_t[DRAW_BUF_SIZE];
  lv_display_t *disp;
  //disp = lv_tft_espi_create(TFT_HOR_RES, TFT_VER_RES, draw_buf, DRAW_BUF_SIZE);
  disp = lv_tft_espi_create(TFT_HOR_RES, TFT_VER_RES, draw_buf, DRAW_BUF_SIZE);
  // Start the tft display
  tft.init();
  // Set the TFT display rotation in portrait mode

  // Configure LEDC timer
  ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_MODE,
    .duty_resolution = LEDC_DUTY_RES,
    .timer_num = LEDC_TIMER,
    .freq_hz = LEDC_FREQUENCY,
    .clk_cfg = LEDC_AUTO_CLK
  };
  ledc_timer_config(&ledc_timer);

  // Configure LEDC channel
  ledc_channel_config_t ledc_channel = {
    .gpio_num = LEDC_OUTPUT_IO,
    .speed_mode = LEDC_MODE,
    .channel = LEDC_CHANNEL,
    .timer_sel = LEDC_TIMER,
    .duty = 4095,  // Set initial duty to max (12-bit resolution, 2^12 - 1)
    .hpoint = 0
  };
  ledc_channel_config(&ledc_channel);

#if orientation == LANDSCAPE
  tft.setRotation(3);
#endif
#if orientation == PORTRAIT
  tft.setRotation(2);
#endif



  //backLight.setBrightness(brightness);
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);



  wifiStatusLabel = lv_label_create(lv_scr_act());  // Use lv_scr_act() to create it on the active (main) screen
  lv_label_set_text(wifiStatusLabel, "Not connected");
  lv_obj_align(wifiStatusLabel, LV_ALIGN_TOP_MID, 0, 0);  // Adjust positioning as needed

  //clearPreferences();

  ledcAnalogWrite(LEDC_CHANNEL_0, 255);
  create_tabs();
  connect_to_wifi();
  Serial.println(F("Setup done"));
}


void setupServer() {

  Serial.println("Setting up server...");  // Add this line
  // Add try-catch to see if there are any exceptions
  try {
    // Serve static files from SPIFFS
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", String(), false);
    });

    server.on("/stations", HTTP_GET, [](AsyncWebServerRequest *request) {
      size_t stationCount;
      RadioStation *stations = loadStations(stationCount);

      DynamicJsonDocument json(1024);
      JsonArray array = json.to<JsonArray>();
      for (size_t i = 0; i < stationCount; ++i) {
        JsonObject obj = array.createNestedObject();
        obj["name"] = stations[i].name;
        obj["url"] = stations[i].url;
      }

      String jsonString;
      serializeJson(json, jsonString);
      request->send(200, "application/json", jsonString);

      delete[] stations;  // Free the allocated memory
    });
    server.on(
      "/save_stations", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument json(1024);
        deserializeJson(json, data);
        size_t stationCount = json.size();
        RadioStation *stations = new RadioStation[stationCount];

        for (size_t i = 0; i < stationCount; ++i) {
          stations[i].name = json[i]["name"].as<String>();
          stations[i].url = json[i]["url"].as<String>();
        }

        saveStations(stations, stationCount);

        delete[] stations;
        request->send(200, "text/plain", "Stations saved");
        delay(500);
        resetDevice();
      });




    Serial.println("404 handler configured...");  // Add this line
    server.begin();
    Serial.println("HTTP server started");
  } catch (const std::exception &e) {
    Serial.print("Error in setupServer: ");
    Serial.println(e.what());
  } catch (...) {
    Serial.println("Unknown error in setupServer");
  }
}


void loop() {
  // put your main code here, to run repeatedly:
  lv_task_handler();
  // server.handleClient();
  audio.loop();
  // put your main code here, to run repeatedly:
  static unsigned long previousMillis = 0;  // Variable to store the previous time
  const unsigned long interval = 1000;      // Interval in milliseconds (1 second)

  unsigned long currentMillis = millis();  // Get the current time

  // Check if the interval has elapsed
  if (currentMillis - previousMillis >= interval) {
    // Update the previous time to the current time
    previousMillis = currentMillis;

    // Increment the second count

    seconds++;

    // Output the current time to the serial monitor
    if (seconds > 10) {
      ledcAnalogWrite(LEDC_CHANNEL_0, 0);
    }
  }
  //SET AUDIO VOL ACCORDING TO POTI
  //potValue = analogRead(POTI_PIN);
  //valculate from potValue to Volume.
  //volume = map(potValue, 0, 4095, 255, 0); // Map it to the range 0-255
  //audio.setVolume(volume);
}

void audio_showstreamtitle(const char *info) {
  Serial.print(F("streamtitle "));
  Serial.println(info);
  //printTitle(info);
}



void saveStations(const RadioStation stations[], size_t stationCount) {
  preferences.begin("stations", false);  // Open the namespace "stations" for writing

  preferences.putUInt("station_count", stationCount);  // Save the number of stations

  for (size_t i = 0; i < stationCount; ++i) {
    // Create unique keys for each name and URL
    String nameKey = "name_" + String(i);
    String urlKey = "url_" + String(i);

    // Store each name and URL as a string
    preferences.putString(nameKey.c_str(), stations[i].name);
    preferences.putString(urlKey.c_str(), stations[i].url);
  }

  preferences.end();  // Close the preferences
  
}
static void resetDevice() {
    Serial.println("Resetting device...");
    delay(3000); // Give some time for the serial output to be printed
    ESP.restart();
}

RadioStation *loadStations(size_t &stationCount) {
  preferences.begin("stations", true);  // Open the namespace "stations" for reading

  // Read the number of saved stations
  stationCount = preferences.getUInt("station_count", 0);

  // Allocate memory for the stations array
  RadioStation *stations = new RadioStation[stationCount];

  for (size_t i = 0; i < stationCount; ++i) {
    String nameKey = "name_" + String(i);
    String urlKey = "url_" + String(i);

    // Read each name and URL
    stations[i].name = preferences.getString(nameKey.c_str(), "");
    stations[i].url = preferences.getString(urlKey.c_str(), "");
  }

  preferences.end();  // Close the preferences
  return stations;
}
