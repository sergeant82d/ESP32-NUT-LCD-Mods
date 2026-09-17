#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include "IUSBHostUPS.h"

// Drives the Elecrow CrowPanel Advance 2.4" screen when esp32-nut is built
// for the elecrow-2.4-onboard environment (BOARD_HAS_LCD). Reads UPS data
// directly from the in-process USBHostUPS instance -- no network hop.
class LcdDisplay {
 public:
  void begin();

  // Call every main loop() iteration; pumps LVGL's timer/redraw handler.
  void loop();

  // Setup / captive-portal screen (shown while is_ap_mode == true).
  void showApMode(const String &ap_ssid, const String &ap_password);

  // Main dashboard, mirrors the web UI's "UPS Telemetry" page.
  void updateDashboard(const IUSBHostUPS *ups, bool wifiConnected, const String &wifiSsid);

 private:
  bool _began = false;
  bool _showingApScreen = false;
};

extern LcdDisplay lcd_display;

#endif  // LCD_DISPLAY_H
