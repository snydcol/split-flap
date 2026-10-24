/* ####################################################################################################################### */
/* # ____  ____  _     ___ _____   _____ _        _    ____    _____ ____  ____    __  __    _    ____ _____ _____ ____  # */
/* #/ ___||  _ \| |   |_ _|_   _| |  ___| |      / \  |  _ \  | ____/ ___||  _ \  |  \/  |  / \  / ___|_   _| ____|  _ \ # */
/* #\___ \| |_) | |    | |  | |   | |_  | |     / _ \ | |_) | |  _| \___ \| |_) | | |\/| | / _ \ \___ \ | | |  _| | |_) |# */
/* # ___) |  __/| |___ | |  | |   |  _| | |___ / ___ \|  __/  | |___ ___) |  __/  | |  | |/ ___ \ ___) || | | |___|  _ < # */
/* #|____/|_|   |_____|___| |_|   |_|   |_____/_/   \_|_|     |_____|____/|_|     |_|  |_/_/   \_|____/ |_| |_____|_| \_\# */
/* ####################################################################################################################### */
/*
  This project is done for fun as part of: https://github.com/JonnyBooker/split-flap
  None of this would be possible without the brilliant work of David Königsmann: https://github.com/Dave19171/split-flap

  Licensed under GNU: https://github.com/JonnyBooker/split-flap/blob/master/LICENSE
*/

/**
 * ESPMaster is the ESP8266 coordinator for the split-flap display.
 *
 * It connects the display to Wi-Fi, serves the web UI from LittleFS, keeps
 * time synchronized, and sends target flap positions to the Arduino units
 * over I2C. The remaining .ino files contain the focused services used by
 * this sketch; Arduino combines them into one compilation unit.
 *
 * Startup order is important:
 *   1. Configure the serial or I2C interface.
 *   2. Connect to Wi-Fi or open the setup portal.
 *   3. Synchronize the clock and load persisted settings.
 *   4. Register the web routes and start the server.
 *
 * The main loop then handles deferred reboots/resets, OTA work, time events,
 * scheduled messages, countdown updates, and the active display mode.
 */

/* .--------------------------------------------------------------------------------. */
/* |  ___           __ _                    _    _       ___       __ _             | */
/* | / __|___ _ _  / _(_)__ _ _  _ _ _ __ _| |__| |___  |   \ ___ / _(_)_ _  ___ ___| */
/* || (__/ _ | ' \|  _| / _` | || | '_/ _` | '_ | / -_) | |) / -_|  _| | ' \/ -_(_-<| */
/* | \___\___|_||_|_| |_\__, |\_,_|_| \__,_|_.__|_\___| |___/\___|_| |_|_||_\___/__/| */
/* |                    |___/                                                       | */
/* '--------------------------------------------------------------------------------' */
/*
  These compile-time switches change the functionality and behaviour of the
  device. Rebuild the sketch after changing any of them.
*/
#define SERIAL_ENABLE       false   //Enable serial diagnostics. On an ESP-01 this uses the pins needed for I2C.
#define UNIT_CALLS_DISABLE  false   //Skip I2C unit calls and simulate display updates while testing the ESP alone.
#define OTA_ENABLE          true    //Enable Arduino OTA update support.
#define UNITS_AMOUNT        10      //Number of connected units; must match the physical display.
#define SERIAL_BAUDRATE     115200  //Baud rate used when serial diagnostics are enabled.
#define WIFI_USE_DIRECT     false   //Use compiled credentials when true; use the WiFiManager AP portal when false.
#define USE_MULTICAST       false   //Advertise mdnsName as a ".local" address on the local network.

/*
  EXPERIMENTAL: Configure a static address in the settings below. Prefer
  reserving an address in the router where possible so it cannot conflict with
  another device on the network.
*/
#define WIFI_STATIC_IP      false

/* .--------------------------------------------------------. */
/* | ___         _               ___       __ _             | */
/* |/ __|_  _ __| |_ ___ _ __   |   \ ___ / _(_)_ _  ___ ___| */
/* |\__ | || (_-|  _/ -_| '  \  | |) / -_|  _| | ' \/ -_(_-<| */
/* ||___/\_, /__/\__\___|_|_|_| |___/\___|_| |_|_||_\___/__/| */
/* |     |__/                                               | */
/* '--------------------------------------------------------' */
/*
  These values describe the protocol and physical display. They normally stay
  unchanged unless the connected unit firmware or hardware changes.
*/
#define ANSWER_SIZE         1       //Number of bytes returned by a unit status request.
#define FLAP_AMOUNT         45      //Number of positions in each unit's alphabet.
#define MIN_SPEED           1       //Slowest speed accepted by a unit.
#define MAX_SPEED           12      //Fastest speed accepted by a unit.

/* .-----------------------------------. */
/* | _    _ _                 _        | */
/* || |  (_| |__ _ _ __ _ _ _(_)___ ___| */
/* || |__| | '_ | '_/ _` | '_| / -_(_-<| */
/* ||____|_|_.__|_| \__,_|_| |_\___/__/| */
/* '-----------------------------------' */
/*
  External library dependencies used by the ESP8266 master.
*/

//WiFiManager is only needed when the device provides its own setup portal.
//Keep this include before the async web server headers to avoid library conflicts.
#if WIFI_USE_DIRECT == false
//Needed in order to be compatible with WiFiManager: https://github.com/me-no-dev/ESPAsyncWebServer/issues/418#issuecomment-667976368
#define WEBSERVER_H
#include <WiFiManager.h>
#endif

//ArduinoOTA is optional so installations that do not need OTA can omit it.
#if OTA_ENABLE == true
#include <ArduinoOTA.h>
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ezTime.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "Classes.h"
#include "LittleFS.h"
/* .------------------------------------------------------------------------------------. */
/* |  ___           __ _                    _    _       ___     _   _   _              | */
/* | / __|___ _ _  / _(_)__ _ _  _ _ _ __ _| |__| |___  / __|___| |_| |_(_)_ _  __ _ ___| */
/* || (__/ _ | ' \|  _| / _` | || | '_/ _` | '_ | / -_) \__ / -_|  _|  _| | ' \/ _` (_-<| */
/* | \___\___|_||_|_| |_\__, |\_,_|_| \__,_|_.__|_\___| |___\___|\__|\__|_|_||_\__, /__/| */
/* |                    |___/                                                  |___/    | */
/* '------------------------------------------------------------------------------------' */
/*
  User-configurable settings. These values can be changed without altering the
  display logic below.
*/
//Used when WIFI_USE_DIRECT is true. Leave blank when using WiFiManager.
const char* wifiDirectSsid = "";
const char* wifiDirectPassword = "";

//Optional password required by OTA uploads. Leave blank to disable OTA authentication.
const char* otaPassword = "";

//TZ database name used when converting synchronized UTC time for the display.
//https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
const char* timezoneString = "Europe/London";
//Optional POSIX rules for a custom timezone. Leave empty when timezoneString is sufficient.
//Example for Central European Time: const char* timezonePosix = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* timezonePosix = "";

//Optional NTP server for time synchronization. Empty uses the library default.
//Example: const char* timezoneServer = "pool.ntp.org";
const char* timezoneServer = "";


//ezTime format strings used by date and clock display modes.
//Complete table with every char: https://github.com/ropg/ezTime#getting-date-and-time
const char* dateFormat = "d.m.Y"; //Examples: d.m.Y -> 11.09.2021, D M y -> SAT SEP 21
const char* clockFormat = "H:i"; //Examples: H:i -> 21:19, h:ia -> 09:19PM

//Duration for scheduled messages that are not marked "Show Indefinitely".
const int scheduledMessageDisplayTimeMillis = 7500;

//mDNS name advertised when USE_MULTICAST is enabled. Use a unique name for
//each display on the same network.
const char* mdnsName = "split-flap";

#if WIFI_STATIC_IP == true
//Static address for the ESP8266. It must be outside any conflicting allocation.
IPAddress wifiDeviceStaticIp(192, 168, 1, 100);

//Router network settings used with the static address.
IPAddress wifiRouterGateway(192, 168, 1, 1);
IPAddress wifiSubnet(255, 255, 0, 0);

//DNS server used by the ESP8266.
IPAddress wifiPrimaryDns(8, 8, 8, 8);
#endif

/* .------------------------------------------------------------. */
/* | ___         _               ___     _   _   _              | */
/* |/ __|_  _ __| |_ ___ _ __   / __|___| |_| |_(_)_ _  __ _ ___| */
/* |\__ | || (_-|  _/ -_| '  \  \__ / -_|  _|  _| | ' \/ _` (_-<| */
/* ||___/\_, /__/\__\___|_|_|_| |___\___|\__|\__|_|_||_\__, /__/| */
/* |     |__/                                          |___/    | */
/* '------------------------------------------------------------' */
/*
  Runtime constants and state. These values are shared by the service files,
  so changing them can affect the web API, persistence format, or unit protocol.
*/
//Version reported by the web UI and /settings endpoint.
const char* espVersion = "2.2.0";

//Flap alphabet in physical order. The array index is sent to each unit.
const char letters[] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '$', '&', '#', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '.', '-', '?', '!'};
int displayState[UNITS_AMOUNT];
unsigned long previousMillis = 0;

//HTTP form/query parameter names used by the web UI.
const char* PARAM_ALIGNMENT = "alignment";
const char* PARAM_FLAP_SPEED = "flapSpeed";
const char* PARAM_DEVICEMODE = "deviceMode";
const char* PARAM_INPUT_TEXT = "inputText";
const char* PARAM_SCHEDULE_ENABLED = "scheduleEnabled";
const char* PARAM_SCHEDULE_DATE_TIME = "scheduledDateTimeUnix";
const char* PARAM_SCHEDULE_SHOW_INDEFINITELY = "scheduleShowIndefinitely";
const char* PARAM_COUNTDOWN_DATE = "countdownDateTimeUnix";
const char* PARAM_ID = "id";

//Values accepted by the deviceMode form field and persisted setting.
const char* DEVICE_MODE_TEXT = "text";
const char* DEVICE_MODE_CLOCK = "clock";
const char* DEVICE_MODE_DATE = "date";
const char* DEVICE_MODE_COUNTDOWN = "countdown";

//Values accepted by the alignment form field and persisted setting.
const char* ALIGNMENT_MODE_LEFT = "left";
const char* ALIGNMENT_MODE_CENTER = "center";
const char* ALIGNMENT_MODE_RIGHT = "right";

//LittleFS paths for settings and the scheduled-message queue.
const char* alignmentPath = "/alignment.txt";
const char* flapSpeedPath = "/flapspeed.txt";
const char* deviceModePath = "/devicemode.txt";
const char* countdownPath = "/countdown.txt";
const char* scheduledMessagesPath = "/scheduled-messages.txt";

//Runtime settings loaded from LittleFS or received from the web UI.
String alignment = "";
String flapSpeed = "";
String inputText = "";
String deviceMode = "";
String countdownToDateUnix = "";
String lastWrittenText = "";
String lastReceivedMessageDateTime = "";
bool alignmentUpdated = false;
bool isPendingReboot = false;
bool isPendingUnitsReset = false;
bool isWifiConfigured = false;
LList<ScheduledMessage> scheduledMessages;
Timezone timezone; 

//The web UI and JSON endpoints are served over the default HTTP port.
AsyncWebServer webServer(80);

//WiFiManager state is only needed in AP/provisioning mode.
#if WIFI_USE_DIRECT == false
WiFiManager wifiManager;
bool isPendingWifiReset = false;
#endif

//OTA handlers are initialized lazily when /ota is requested.
#if OTA_ENABLE == true
bool isInOtaMode = false;
#endif

/* .-----------------------------------------------. */
/* | ___          _          ___     _             | */
/* ||   \ _____ _(_)__ ___  / __|___| |_ _  _ _ __ | */
/* || |) / -_\ V | / _/ -_) \__ / -_|  _| || | '_ \| */
/* ||___/\___|\_/|_\__\___| |___\___|\__|\_,_| .__/| */
/* |                                         |_|   | */
/* '-----------------------------------------------' */
/**
 * Initializes hardware, networking, time, persistence, and HTTP routes.
 *
 * The web server is only started after Wi-Fi is configured and the persisted
 * settings have been loaded. Requests that require a reboot set a flag and
 * are acted on from loop(), allowing the current HTTP response to finish.
 */
void setup() {
#if SERIAL_ENABLE == true
  //Serial diagnostics and I2C cannot share the ESP-01 pins in this build.
  Serial.begin(SERIAL_BAUDRATE);
#else
  //ESP-01 I2C pins: SDA=GPIO1 and SCL=GPIO3.
  Wire.begin(1, 3); 
  
  //For NodeMCU testing, use the board's exposed pins instead:
  //Wire.begin(D1, D2); //SDA=D1, SCL=D2
#endif
  SerialPrintln("");
  SerialPrintln("#######################################################");
  SerialPrintln("..............Split Flap Display Starting..............");
  SerialPrintln("#######################################################");

  //Connect before initializing services that need network time or HTTP access.
  initWiFi();
  
  //Helpful if want to force reset WiFi settings for testing
  //wifiManager.resetSettings();

  if (isWifiConfigured && !isPendingReboot) {
    //Initialize ezTime only after Wi-Fi is available.

    if (strlen(timezoneServer) > 0) {  // Check if timezoneServer is not empty
      setServer(timezoneServer);
    } else {
      SerialPrintln("No timezone server specified, skipping setServer()");
    }
    
    waitForSync();
    timezone.setLocation(timezoneString);

    if (strlen(timezonePosix) > 0) {  // Check if timezonePosix is not empty
      timezone.setPosix(timezonePosix);
    } else {
      SerialPrintln("No timezone Posix specified, skipping setPosix()");
    }
    setDebug(DEBUG);  // if you want to see the communication to the server and the logs 

    SerialPrintln("Current time: ");
    SerialPrintln(timezone.dateTime());
    
    //Mount persistence and restore settings before registering routes.
    initialiseFileSystem();
    loadValuesFromFileSystem();

#if OTA_ENABLE == true
    SerialPrintln("OTA is enabled! Yay!");
#endif

#if USE_MULTICAST == true
  if (MDNS.begin(mdnsName)) { 
      SerialPrintln("mDNS responder started");
    } else {
      SerialPrintln("Error setting up MDNS responder!");
    }
#endif

    //Serve the static web UI and register its JSON/control endpoints.
    webServer.serveStatic("/", LittleFS, "/");
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request Home Page Received");

      request->send(LittleFS, "/index.html", "text/html");
    });

    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request for Settings Received");
      
      String json = getCurrentSettingValues();
      request->send(200, "application/json", json);
      json = String();
    });
    
    webServer.on("/health", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request for Health Check Received");
      request->send(200, "text/plain", "Healthy");
    });
    
    webServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Reboot Received");
      
      //Finish the response before loop() performs the restart.
      IPAddress ip = WiFi.localIP();
      
      String html = "<div style='text-align:center'>";
      html += "<font face='arial'><h1>Split Flap - Rebooting</h1>";
      html += "<p>Reboot is pending now...<p>";
      html += "<p>This can take anywhere between 10-20 seconds<p>";
      html += "<p>You can go to the main home page after this time by clicking the button below or going to '/'.</p>";
      html += "<p><a href=\"http://" + ip.toString() + "\">Home</a></p>";
      html += "</font>";
      html += "</div>";
      
      request->send(200, "text/html", html);
      isPendingReboot = true;
    });
    
    webServer.on("/reset-units", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Reset Units Received");
      
      //The I2C reset sequence runs from loop(), outside the request handler.
      isPendingUnitsReset = true;
      
      request->redirect("/?is-resetting-units=true");
    });

    webServer.on("/scheduled-message/remove", HTTP_DELETE, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Remove Scheduled Message Received");
      
      if (request->hasParam(PARAM_ID)) {
        bool removedScheduledMessage = false;
        String idValue = request->getParam(PARAM_ID)->value();

        if (isNumber(idValue)) {
          long parsedIdValue = atol(idValue.c_str());
          bool removed = removeScheduledMessage(parsedIdValue);
          
          if (removed) {
            request->send(202, "text/plain", "Removed");
          }
          else {
            request->send(400, "text/plain", "Unable to find message with ID specified. Id: " + idValue);
          }
        }
        else {
          SerialPrintln("Invalid Delete Scheduled Message ID Received");
          request->send(400, "text/plain", "Invalid ID value");
        }
      } 
      else {
          SerialPrintln("Delete Scheduled Message Received with no ID");
          request->send(400, "text/plain", "No ID specified");
      }
    });

    webServer.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request Post of Form Received");    

      bool submissionError = false;
      
      long newMessageScheduleDateTimeUnixValue;
      bool newMessageScheduleEnabledValue, newMessageScheduleShowIndefinitely;
      String newAlignmentValue, newDeviceModeValue, newFlapSpeedValue, newInputTextValue, newCountdownToDateUnixValue;
      
      int params = request->params();
      for (int paramIndex = 0; paramIndex < params; paramIndex++) {
        AsyncWebParameter* p = request->getParam(paramIndex);
        if (p->isPost()) {
          //HTTP POST alignment value
          if (p->name() == PARAM_ALIGNMENT) {
            String receivedValue = p->value();
            if (receivedValue == ALIGNMENT_MODE_LEFT || receivedValue == ALIGNMENT_MODE_CENTER || receivedValue == ALIGNMENT_MODE_RIGHT) {
              newAlignmentValue = receivedValue;
            }
            else {
              SerialPrintln("Alignment provided was not valid. Value: " + receivedValue); 
              submissionError = true;
            }
          }

          //HTTP POST device mode value
          if (p->name() == PARAM_DEVICEMODE) {
            String receivedValue = p->value();
            if (receivedValue == DEVICE_MODE_TEXT || receivedValue == DEVICE_MODE_CLOCK || receivedValue == DEVICE_MODE_DATE || receivedValue == DEVICE_MODE_COUNTDOWN) {
              newDeviceModeValue = receivedValue;          
            }
            else {
              SerialPrintln("Device Mode provided was not valid. Invalid Value: " + receivedValue); 
              submissionError = true;
            }
          }

          //HTTP POST Flap Speed Slider value
          if (p->name() == PARAM_FLAP_SPEED) {
            newFlapSpeedValue = p->value().c_str();
          }

          //HTTP POST inputText value
          if (p->name() == PARAM_INPUT_TEXT) {
            newInputTextValue = p->value().c_str();
          }

          //HTTP POST Schedule Enabled
          if (p->name() == PARAM_SCHEDULE_ENABLED) {
            String newMessageScheduleEnabledString = p->value().c_str();
            newMessageScheduleEnabledValue = newMessageScheduleEnabledString == "on" ?
              true : 
              false;
          }
          
          //HTTP POST Schedule Show Indefinitely
          if (p->name() == PARAM_SCHEDULE_SHOW_INDEFINITELY) {
            String newMessageScheduleShowIndefinitelyString = p->value().c_str();
            newMessageScheduleShowIndefinitely = newMessageScheduleShowIndefinitelyString == "on" ?
              true : 
              false;
          }

          //HTTP POST Schedule Seconds
          if (p->name() == PARAM_SCHEDULE_DATE_TIME) {
            String receivedValue = p->value().c_str();
            if (isNumber(receivedValue)) {
              newMessageScheduleDateTimeUnixValue = atol(receivedValue.c_str());
            }
            else {
              SerialPrintln("Schedule date time provided was not valid. Invalid Value: " + receivedValue); 
              submissionError = true;
            }
          }

          //HTTP POST Countdown Seconds
          if (p->name() == PARAM_COUNTDOWN_DATE) {
            String receivedValue = p->value().c_str();
            if (isNumber(receivedValue)) {
              newCountdownToDateUnixValue = receivedValue;
            }
            else {
              SerialPrintln("Countdown date provided was not valid. Invalid Value: " + receivedValue); 
              submissionError = true;
            }
          }
        }
      }    

      //If there was an error, report back to check what has been input
      if (submissionError) {
        SerialPrintln("Finished Processing Request with Error");
        request->redirect("/?invalid-submission=" + true);
      }
      else {
        SerialPrintln("Finished Processing Request Successfully");

        lastReceivedMessageDateTime = timezone.dateTime("d M y H:i:s");

        //Only if a new alignment value
        if (alignment != newAlignmentValue) {
          alignment = newAlignmentValue;
          alignmentUpdated = true;

          writeFile(LittleFS, alignmentPath, alignment.c_str());
          SerialPrintln("Alignment Updated: " + alignment);
        }

        //Only if a new flap speed value
        if (flapSpeed != newFlapSpeedValue) {
          flapSpeed = newFlapSpeedValue;

          writeFile(LittleFS, flapSpeedPath, flapSpeed.c_str());
          SerialPrintln("Flap Speed Updated: " + flapSpeed);
        }

        //Only if countdown date has changed
        if (countdownToDateUnix != newCountdownToDateUnixValue) {
          countdownToDateUnix = newCountdownToDateUnixValue;

          writeFile(LittleFS, countdownPath, countdownToDateUnix.c_str());
          SerialPrintln("Countdown Date Time Unix Updated: " + countdownToDateUnix);
        }

        //If its a new scheduled message, add it to the backlog and proceed, don't want to change device mode
        //Else, we do want to change the device mode and clear out the input text
        if (newMessageScheduleEnabledValue) {
          addAndPersistScheduledMessage(newInputTextValue, newMessageScheduleDateTimeUnixValue, newMessageScheduleShowIndefinitely);
          SerialPrintln("New Scheduled Message added");
        }
        else {
          //Only if device mode has changed
          if (deviceMode != newDeviceModeValue) {
            deviceMode = newDeviceModeValue;

            writeFile(LittleFS, deviceModePath, deviceMode.c_str());
            SerialPrintln("Device Mode Set: " + deviceMode);
          }

          //Only if we are showing text
          if (deviceMode == DEVICE_MODE_TEXT) {
            inputText = newInputTextValue;
          }
        }

        //Redirect so that we don't have the "re-submit form" problem in browser for refresh
        request->redirect("/");
      }
    });

#if OTA_ENABLE == true
    webServer.on("/ota", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to start OTA mode received");
      
      //Finish the response before OTA callbacks take over the device.
      IPAddress ip = WiFi.localIP();
      
      String html = "<div style='text-align:center'>";
      html += "<font face='arial'><h1>Split Flap - OTA Update Mode</h1>";
      html += "<p>OTA mode has been started. You can now update your module via WiFI.<p>";
      html += "<p>Open your Arduino IDE and select the new port in \"Tools\" menu and upload the your sketch as normal!</p>";
      html += "<p>After you have carried out your update, the system will automatically be rebooted. You can go to the main home page after this time by clicking the button below or going to '/'.</p>";
      html += "<p>You can take the system out of this mode by clicking the button to reboot below or going to '/reboot'.</p>";
      html += "<p><a href=\"http://" + ip.toString() + "\")\">Home</a></p>";
      html += "<p><a href=\"http://" + ip.toString() + "/reboot\")\">Reboot</a></p>";
      html += "</font>";
      html += "</div>";

      request->send(200, "text/html", html);
  
      if (!isInOtaMode) {
        SerialPrintln("Setting OTA Hostname");
        ArduinoOTA.setHostname("Split-Flap-OTA");

        //Authentication is optional and disabled by default for ease of setup.
        if (otaPassword != "") {
          ArduinoOTA.setPassword(otaPassword);
        }
        
        SerialPrintln("Starting OTA Mode");
        ArduinoOTA.begin();
        delay(100);
      
        ArduinoOTA.onStart([]() {
          LittleFS.end();
          if (ArduinoOTA.getCommand() == U_FLASH) {
            SerialPrintln("Start updating sketch");
          } 
          else {
            SerialPrintln("Start updating filesystem");
          }  
        });
        
        ArduinoOTA.onEnd([]() {
          SerialPrintln("Finished OTA Update - Rebooting");
          isPendingReboot = true;
        });
        
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
          SerialPrintf("OTA Progress: %u%%\r", (progress / (total / 100)));
        });
        
        ArduinoOTA.onError([](ota_error_t error) {
          SerialPrintf("Error[%u]: ", error);

          if (error == OTA_AUTH_ERROR) {
            SerialPrintln("Finished OTA Update - Rebooting");
          }
          else if (error == OTA_BEGIN_ERROR) {
            SerialPrintln("OTA Begin Failed");
          }
          else if (error == OTA_CONNECT_ERROR) {
            SerialPrintln("OTA Connect Failed");
          }
          else if (error == OTA_RECEIVE_ERROR) {
            SerialPrintln("OTA Receive Failed");
          }
          else if (error == OTA_END_ERROR) {
            SerialPrintln("OTA End Failed");
          }
        });
        
        //loop() calls ArduinoOTA.handle() while this flag is set.
        isInOtaMode = true;
      }
      else {
        SerialPrintln("Already in OTA Mode");
      }
    });
#endif

#if WIFI_USE_DIRECT == false
    webServer.on("/reset-wifi", HTTP_GET, [](AsyncWebServerRequest * request) {
      SerialPrintln("Request to Reset WiFi Received");
      
      IPAddress ip = WiFi.localIP();
      
      String html = "<div style='text-align:center'>";
      html += "<font face='arial'><h1>Split Flap - Resetting WiFi</h1>";
      html += "<p>WiFi Settings have been erased. Device will now reboot...<p>";
      html += "<p>You will now be able to connect to this device in AP mode to configure the WiFi once more<p>";
      html += "<p>You can go to the main home page after this time by clicking the button below or going to '/'.</p>";
      html += "<p><a href=\"http://" + ip.toString() + "\">Home</a></p>";
      html += "</font>";
      html += "</div>";
      
      request->send(200, "text/html", html);
      isPendingWifiReset = true;
    });
#endif   

    //Give the async server a moment to finish initialization before accepting requests.
    delay(250);
    webServer.begin();

    SerialPrintln("Split Flap Ready!");
    SerialPrintln("#######################################################");
  }
  else {
    if (isPendingReboot) {
      SerialPrintln("Reboot is pending to be able to continue device function. Hold please...");
      SerialPrintln("#######################################################");
    }
    else {
      SerialPrintln("Unable to connect to WiFi... Not starting web server");
      SerialPrintln("Please hard restart your device to try connect again");
      SerialPrintln("#######################################################");
    }
  }
}

/* .----------------------------------------------------. */
/* | ___                _             _                 | */
/* || _ \_  _ _ _  _ _ (_)_ _  __ _  | |   ___ ___ _ __ | */
/* ||   | || | ' \| ' \| | ' \/ _` | | |__/ _ / _ | '_ \| */
/* ||_|_\\_,_|_||_|_||_|_|_||_\__, | |____\___\___| .__/| */
/* |                          |___/               |_|   | */
/* '----------------------------------------------------' */
/**
 * Runs deferred control actions and refreshes the active display mode.
 *
 * Reboots and Wi-Fi resets have priority because continuing normal display
 * work after either request could leave the device in a partially configured
 * state. Normal display calculations run once per second; OTA and ezTime
 * service work continues between those ticks.
 */
void loop() {
  //Restart from loop() so the HTTP response can be returned to the caller.
  if (isPendingReboot) {
    SerialPrintln("Rebooting Now... Fairwell!");
    SerialPrintln("#######################################################");
    delay(100);

    ESP.restart();
    return;
  }

#if WIFI_USE_DIRECT == false
  //Erase WiFiManager settings from loop(), then reboot into setup mode.
  if (isPendingWifiReset) {
    SerialPrintln("Removing WiFi settings");
    wifiManager.resetSettings();
    delay(100);

    isPendingReboot = true;
    return;
  }
#endif

#if USE_MULTICAST == true
  MDNS.update();
#endif

  //Without Wi-Fi there is no clock or web service, so show a clear local status.
  if (!isWifiConfigured) {
    //Show there is an error via text on display
    deviceMode = DEVICE_MODE_TEXT;
    alignment = ALIGNMENT_MODE_CENTER;
    flapSpeed = "80";

    showText("OFFLINE");
    delay(100);
    return;
  }

  if (isPendingUnitsReset) {
    SerialPrintln("Reseting Units now...");

    //A full sweep of '-' and '.' causes each unit to re-home/recalibrate.
    String blankOutText1 = createRepeatingString('-');
    showText(blankOutText1);
    delay(2000);

    //Do just enough to do a full iteration which triggers the re-calibration
    String blankOutText2 = createRepeatingString('.');
    showText(blankOutText2);

    //We did a reset!
    isPendingUnitsReset = false;

    SerialPrintln("Done Units Reset!");
  }
  
#if OTA_ENABLE == true
  //Service OTA traffic while the device is in update mode.
  if(isInOtaMode) {
    ArduinoOTA.handle();
    delay(1);
  }
#endif

  //Allow ezTime to process NTP synchronization and time events.
  events(); 
  
  //Process display state at a human-readable one-second cadence.
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= 1000) {
    previousMillis = currentMillis;

    checkScheduledMessages();
    checkCountdown();

    //Select the text source for the currently active device mode.
    if (deviceMode == DEVICE_MODE_TEXT || deviceMode == DEVICE_MODE_COUNTDOWN) { 
      showText(inputText);
    } 
    else if (deviceMode == DEVICE_MODE_DATE) {
      showText(timezone.dateTime(dateFormat));
    } 
    else if (deviceMode == DEVICE_MODE_CLOCK) {
      showText(timezone.dateTime(clockFormat));
    } 
  }
}

/**
 * Serializes the current runtime settings and scheduled messages for /settings.
 *
 * The response is generated from memory rather than re-reading LittleFS so it
 * also reflects transient values such as the last message and OTA state.
 */
String getCurrentSettingValues() {
  JsonDocument document;

  document["timezoneOffset"] = timezone.getOffset();
  document["unitCount"] = UNITS_AMOUNT;
  document["alignment"] = alignment;
  document["flapSpeed"] = flapSpeed;
  document["deviceMode"] = deviceMode;
  document["version"] = espVersion;
  document["lastTimeReceivedMessageDateTime"] = lastReceivedMessageDateTime;
  document["lastWrittenText"] = lastWrittenText;
  document["countdownToDateUnix"] = atol(countdownToDateUnix.c_str());

  for(int scheduledMessageIndex = 0; scheduledMessageIndex < scheduledMessages.size(); scheduledMessageIndex++) {
    ScheduledMessage scheduledMessage = scheduledMessages[scheduledMessageIndex];
    
    document["scheduledMessages"][scheduledMessageIndex]["scheduledDateTimeUnix"] = scheduledMessage.ScheduledDateTimeUnix;
    document["scheduledMessages"][scheduledMessageIndex]["message"] = scheduledMessage.Message;
    document["scheduledMessages"][scheduledMessageIndex]["showIndefinitely"] = scheduledMessage.ShowIndefinitely;
  }

#if OTA_ENABLE == true
  document["otaEnabled"] = true;
  document["isInOtaMode"] = isInOtaMode;
#else
  document["otaEnabled"] = false;
#endif

#if WIFI_USE_DIRECT == false
  document["wifiSettingsResettable"] = true;
#else
  document["wifiSettingsResettable"] = false;
#endif
  
  String jsonString;
  serializeJson(document, jsonString);

  return jsonString;
}
