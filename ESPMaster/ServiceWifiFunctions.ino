/**
 * Wi-Fi connection and provisioning service.
 *
 * In setup/AP mode, WiFiManager provides a temporary portal for selecting a
 * network and saving credentials. In direct mode, the credentials compiled
 * into ESPMaster.ino are used instead. A successful connection sets
 * isWifiConfigured, which controls whether the web server and display services
 * are started.
 */

/**
 * Initializes Wi-Fi using either WiFiManager provisioning or direct credentials.
 *
 * When WiFiManager saves new credentials, the callback schedules a reboot
 * because the web server cannot safely be started in the same boot cycle.
 */
void initWiFi() {
  int wifiConnectTimeoutSeconds = 180;

  WiFi.mode(WIFI_STA);

#if WIFI_USE_DIRECT == false
  SerialPrintln("Setting up WiFi AP Setup Mode");

#if WIFI_STATIC_IP == true
  wifiManager.setSTAStaticIPConfig(wifiDeviceStaticIp, wifiRouterGateway, wifiSubnet, wifiPrimaryDns);
  SerialPrintln("WiFi Static IP Configured");
#endif

  wifiManager.setTitle("Split-Flap Setup");
  wifiManager.setHostname("Split-Flap");
  wifiManager.setDarkMode(true);
  wifiManager.setShowInfoUpdate(false);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setConfigPortalTimeout(wifiConnectTimeoutSeconds);
  wifiManager.setConnectTimeout(120);    
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setSaveConfigCallback([]() {
    //If the portal was needed to configure Wi-Fi, reboot before starting the
    //web server. WiFiManager and the web server do not reliably share a boot
    //cycle on this ESP8266 setup:
    //https://github.com/tzapu/WiFiManager/issues/1579
    
    //The documented workaround is to reset the device:
    //https://github.com/rancilio-pid/clevercoffee/issues/323#issuecomment-1587344185

    SerialPrintln("New WiFi configuration saved. Will need to reboot device to let webserver work...");
    isPendingReboot = true;
  });
  
  //Set the menu options
  std::vector<const char *> menu = { "wifi", "info" ,"param", "sep", "restart", "exit" };
  wifiManager.setMenu(menu);

  SerialPrintln("Attempting to connect to WiFi... Will fallback to AP mode to allow configuring of WiFi if fails...");
  if(wifiManager.autoConnect("Split-Flap-AP")) {
    SerialPrint("Successfully Connected to WiFi. IP Address: ");
    SerialPrintln(WiFi.localIP());

    isWifiConfigured = true;
  }
  
#else
  SerialPrintln("Setting up WiFi Direct");

  if (wifiDirectSsid != "" && wifiDirectPassword != "") {
    int maxAttemptsCount = 0;
    
#if WIFI_STATIC_IP == true
    if (WiFi.config(wifiDeviceStaticIp, wifiRouterGateway, wifiSubnet, wifiPrimaryDns)) {
      SerialPrintln("WiFi Static IP Configuration Success");
    }
    else {
      SerialPrintln("WiFi Static IP Configuration could not take place");
    }
#endif
    
    WiFi.begin(wifiDirectSsid, wifiDirectPassword);
    SerialPrint("Connecting");

    while (WiFi.status() != WL_CONNECTED && maxAttemptsCount != wifiConnectTimeoutSeconds) {
      if (maxAttemptsCount % 10 == 0) {
        SerialPrint('\n');
      }
      else {
        SerialPrint('.');
      }

      delay(1000);

      maxAttemptsCount++;      
    }

    //If we reached the max timeout
    if (maxAttemptsCount != wifiConnectTimeoutSeconds) {
      SerialPrint("Successfully Connected to WiFi. IP Address: ");
      SerialPrintln(WiFi.localIP());

      isWifiConfigured = true;
    }
  }

#endif
}
