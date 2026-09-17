#include "LcdDisplay.h"

#include <lvgl.h>
#include <math.h>
#include <esp_heap_caps.h>

#include "lgfx_config.h"

LcdDisplay lcd_display;

// ---------------------------------------------------------------------
// Theme - colors lifted from esp32-nut's own web dashboard (bundle.css)
// ---------------------------------------------------------------------
#define COL_BG lv_color_hex(0x06090E)
#define COL_CARD_BG lv_color_hex(0x0B121C)
#define COL_BORDER lv_color_hex(0x1B2A3A)
#define COL_CYAN lv_color_hex(0x00F0FF)
#define COL_GREEN lv_color_hex(0x00FF9D)
#define COL_AMBER lv_color_hex(0xFFB800)
#define COL_RED lv_color_hex(0xFF3366)
#define COL_LABEL lv_color_hex(0x8A9BB3)
#define COL_TEXT lv_color_hex(0xE0E6ED)

// See the standalone nut-lcd-display project's README for the same note:
// LVGL's built-in Montserrat fonts are used instead of the web UI's
// Orbitron/Share Tech Mono so this compiles with zero extra font-conversion
// steps.

// ---------------------------------------------------------------------
// Display + touch driver instance (see lgfx_config.h)
// ---------------------------------------------------------------------
static LGFX gfx;

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_lv_buf1 = nullptr;
static lv_color_t *s_lv_buf2 = nullptr;

static void disp_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels((lgfx::rgb565_t *)&color_p->full, w * h);
  gfx.endWrite();

  lv_disp_flush_ready(disp);
}

static void touchpad_read_cb(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  if (gfx.getTouch(&touchX, &touchY)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = LCD_H_RES - touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ---------------------------------------------------------------------
// Dashboard screen widgets
// ---------------------------------------------------------------------
struct StatCard {
  lv_obj_t *box;
  lv_obj_t *value;
};

static lv_style_t s_style_card;
static lv_obj_t *s_screen_dashboard;
static lv_obj_t *s_screen_apmode;

static lv_obj_t *s_dot_live;
static lv_obj_t *s_dot_wifi;
static lv_obj_t *s_dot_ups;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_ups;
static lv_obj_t *s_lbl_status_value;
static StatCard s_card_batt, s_card_load, s_card_runtime, s_card_power;

static lv_obj_t *s_ap_ssid_label;
static lv_obj_t *s_ap_pass_label;

static void init_styles() {
  lv_style_init(&s_style_card);
  lv_style_set_bg_color(&s_style_card, COL_CARD_BG);
  lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
  lv_style_set_border_color(&s_style_card, COL_BORDER);
  lv_style_set_border_width(&s_style_card, 1);
  lv_style_set_radius(&s_style_card, 6);
  lv_style_set_pad_all(&s_style_card, 4);
}

static lv_obj_t *make_dot(lv_obj_t *parent) {
  lv_obj_t *dot = lv_obj_create(parent);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, COL_LABEL, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  return dot;
}

static StatCard make_stat_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                                lv_coord_t h, const char *title) {
  StatCard c;
  c.box = lv_obj_create(parent);
  lv_obj_remove_style_all(c.box);
  lv_obj_add_style(c.box, &s_style_card, 0);
  lv_obj_set_size(c.box, w, h);
  lv_obj_set_pos(c.box, x, y);
  lv_obj_clear_flag(c.box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(c.box);
  lv_label_set_text(lbl_title, title);
  lv_obj_set_style_text_color(lbl_title, COL_LABEL, 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

  c.value = lv_label_create(c.box);
  lv_label_set_text(c.value, "--");
  lv_obj_set_style_text_color(c.value, COL_TEXT, 0);
  lv_obj_set_style_text_font(c.value, &lv_font_montserrat_20, 0);
  lv_obj_align(c.value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  return c;
}

static void set_dot_color(lv_obj_t *dot, lv_color_t c) {
  lv_obj_set_style_bg_color(dot, c, 0);
}

// Mirrors UPSData::computeUPSStatusString()'s flag codes.
static lv_color_t status_color(const String &s) {
  if (s.indexOf("LB") >= 0 || s.indexOf("FSD") >= 0 || s.indexOf("COMM_LOST") >= 0) return COL_RED;
  if (s.indexOf("OB") >= 0 || s.indexOf("OVER") >= 0 || s.indexOf("RB") >= 0) return COL_AMBER;
  if (s.indexOf("OL") >= 0) return COL_GREEN;
  return COL_LABEL;
}

static String format_runtime(long sec) {
  if (sec < 0) return "--";
  long m = sec / 60;
  long h = m / 60;
  m = m % 60;
  char buf[16];
  if (h > 0)
    snprintf(buf, sizeof(buf), "%ldh%02ldm", h, m);
  else
    snprintf(buf, sizeof(buf), "%ldm", m);
  return String(buf);
}

static void build_dashboard_screen() {
  s_screen_dashboard = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_dashboard, COL_BG, 0);
  lv_obj_clear_flag(s_screen_dashboard, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_screen_dashboard);
  lv_label_set_text(title, "ESP32-NUT | UPS TELEMETRY");
  lv_obj_set_style_text_color(title, COL_CYAN, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 6);

  s_dot_live = make_dot(s_screen_dashboard);
  lv_obj_align(s_dot_live, LV_ALIGN_TOP_RIGHT, -8, 8);
  set_dot_color(s_dot_live, COL_GREEN);  // always "live": data is local, not fetched

  lv_obj_t *status_box = lv_obj_create(s_screen_dashboard);
  lv_obj_remove_style_all(status_box);
  lv_obj_add_style(status_box, &s_style_card, 0);
  lv_obj_set_size(status_box, LCD_H_RES - 12, 54);
  lv_obj_set_pos(status_box, 6, 28);
  lv_obj_clear_flag(status_box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *status_title = lv_label_create(status_box);
  lv_label_set_text(status_title, "UPS STATUS");
  lv_obj_set_style_text_color(status_title, COL_LABEL, 0);
  lv_obj_set_style_text_font(status_title, &lv_font_montserrat_14, 0);
  lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 0, 0);

  s_lbl_status_value = lv_label_create(status_box);
  lv_label_set_text(s_lbl_status_value, "--");
  lv_obj_set_style_text_color(s_lbl_status_value, COL_TEXT, 0);
  lv_obj_set_style_text_font(s_lbl_status_value, &lv_font_montserrat_32, 0);
  lv_obj_align(s_lbl_status_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  const lv_coord_t gx = 6, gy = 88, gap = 6;
  const lv_coord_t gw = (LCD_H_RES - gx * 2 - gap) / 2;
  const lv_coord_t gh = 50;

  s_card_batt = make_stat_card(s_screen_dashboard, gx, gy, gw, gh, "BATTERY CHARGE");
  s_card_load = make_stat_card(s_screen_dashboard, gx + gw + gap, gy, gw, gh, "UPS LOAD");
  s_card_runtime = make_stat_card(s_screen_dashboard, gx, gy + gh + gap, gw, gh, "RUNTIME");
  s_card_power =
      make_stat_card(s_screen_dashboard, gx + gw + gap, gy + gh + gap, gw, gh, "REAL POWER");

  s_dot_wifi = make_dot(s_screen_dashboard);
  lv_obj_align(s_dot_wifi, LV_ALIGN_TOP_LEFT, 8, 202);
  s_lbl_wifi = lv_label_create(s_screen_dashboard);
  lv_label_set_text(s_lbl_wifi, "Wi-Fi: --");
  lv_obj_set_style_text_color(s_lbl_wifi, COL_LABEL, 0);
  lv_obj_set_style_text_font(s_lbl_wifi, &lv_font_montserrat_14, 0);
  lv_obj_align(s_lbl_wifi, LV_ALIGN_TOP_LEFT, 22, 200);

  s_dot_ups = make_dot(s_screen_dashboard);
  lv_obj_align(s_dot_ups, LV_ALIGN_TOP_LEFT, 8, 220);
  s_lbl_ups = lv_label_create(s_screen_dashboard);
  lv_label_set_text(s_lbl_ups, "UPS: --");
  lv_obj_set_style_text_color(s_lbl_ups, COL_LABEL, 0);
  lv_obj_set_style_text_font(s_lbl_ups, &lv_font_montserrat_14, 0);
  lv_obj_align(s_lbl_ups, LV_ALIGN_TOP_LEFT, 22, 218);
}

static void build_apmode_screen() {
  s_screen_apmode = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_apmode, COL_BG, 0);
  lv_obj_clear_flag(s_screen_apmode, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_screen_apmode);
  lv_label_set_text(title, "SETUP MODE");
  lv_obj_set_style_text_color(title, COL_CYAN, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *instr = lv_label_create(s_screen_apmode);
  lv_label_set_text(instr, "Connect a phone/PC to this Wi-Fi network:");
  lv_obj_set_style_text_color(instr, COL_LABEL, 0);
  lv_obj_set_style_text_font(instr, &lv_font_montserrat_14, 0);
  lv_obj_align(instr, LV_ALIGN_TOP_MID, 0, 46);

  s_ap_ssid_label = lv_label_create(s_screen_apmode);
  lv_label_set_text(s_ap_ssid_label, "--");
  lv_obj_set_style_text_color(s_ap_ssid_label, COL_GREEN, 0);
  lv_obj_set_style_text_font(s_ap_ssid_label, &lv_font_montserrat_20, 0);
  lv_obj_align(s_ap_ssid_label, LV_ALIGN_TOP_MID, 0, 72);

  s_ap_pass_label = lv_label_create(s_screen_apmode);
  lv_label_set_text(s_ap_pass_label, "--");
  lv_obj_set_style_text_color(s_ap_pass_label, COL_TEXT, 0);
  lv_obj_set_style_text_font(s_ap_pass_label, &lv_font_montserrat_14, 0);
  lv_obj_align(s_ap_pass_label, LV_ALIGN_TOP_MID, 0, 104);

  lv_obj_t *hint = lv_label_create(s_screen_apmode);
  lv_label_set_text(hint, "Then open http://192.168.4.1");
  lv_obj_set_style_text_color(hint, COL_LABEL, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 140);
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------
void LcdDisplay::begin() {
  if (_began) return;

  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

  gfx.init();
  gfx.fillScreen(TFT_BLACK);

  lv_init();

  size_t bufBytes = sizeof(lv_color_t) * LCD_H_RES * LCD_V_RES;
  s_lv_buf1 = (lv_color_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  s_lv_buf2 = (lv_color_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&s_draw_buf, s_lv_buf1, s_lv_buf2, LCD_H_RES * LCD_V_RES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = disp_flush_cb;
  disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read_cb;
  lv_indev_drv_register(&indev_drv);

  init_styles();
  build_dashboard_screen();
  build_apmode_screen();

  lv_scr_load(s_screen_dashboard);
  _showingApScreen = false;
  _began = true;
}

void LcdDisplay::loop() {
  if (!_began) return;
  lv_timer_handler();
}

void LcdDisplay::showApMode(const String &ap_ssid, const String &ap_password) {
  if (!_began) return;
  lv_label_set_text(s_ap_ssid_label, ap_ssid.c_str());
  String pw = "Password: " + ap_password;
  lv_label_set_text(s_ap_pass_label, pw.c_str());
  if (!_showingApScreen) {
    lv_scr_load(s_screen_apmode);
    _showingApScreen = true;
  }
}

void LcdDisplay::updateDashboard(const IUSBHostUPS *ups, bool wifiConnected,
                                  const String &wifiSsid) {
  if (!_began) return;

  if (_showingApScreen) {
    lv_scr_load(s_screen_dashboard);
    _showingApScreen = false;
  }

  // Wi-Fi / UPS connection footer
  String wtxt = "Wi-Fi: " + (wifiConnected ? wifiSsid : String("disconnected"));
  lv_label_set_text(s_lbl_wifi, wtxt.c_str());
  set_dot_color(s_dot_wifi, wifiConnected ? COL_GREEN : COL_RED);

  if (!ups || !ups->isConnected()) {
    lv_label_set_text(s_lbl_ups, "UPS: disconnected");
    set_dot_color(s_dot_ups, COL_RED);

    lv_label_set_text(s_lbl_status_value, "DISCONNECTED");
    lv_obj_set_style_text_color(s_lbl_status_value, COL_LABEL, 0);
    lv_label_set_text(s_card_batt.value, "--");
    lv_label_set_text(s_card_load.value, "--");
    lv_label_set_text(s_card_runtime.value, "--");
    lv_label_set_text(s_card_power.value, "--");
    return;
  }

  auto data = ups->getUPSData();  // RAII lock, released at end of scope

  String model = data->hasKey("ups.model") ? data->get("ups.model") : String("Unknown Model");
  lv_label_set_text(s_lbl_ups, ("UPS: " + model).c_str());
  set_dot_color(s_dot_ups, COL_GREEN);

  String status = ups->getUPSStatusString();
  lv_label_set_text(s_lbl_status_value, status.c_str());
  lv_obj_set_style_text_color(s_lbl_status_value, status_color(status), 0);

  char buf[16];

  if (data->hasKey("battery.charge")) {
    snprintf(buf, sizeof(buf), "%d%%", (int)roundf(data->getFloat("battery.charge")));
    lv_label_set_text(s_card_batt.value, buf);
  } else {
    lv_label_set_text(s_card_batt.value, "--");
  }

  if (data->hasKey("ups.load")) {
    snprintf(buf, sizeof(buf), "%d%%", (int)roundf(data->getFloat("ups.load")));
    lv_label_set_text(s_card_load.value, buf);
  } else {
    lv_label_set_text(s_card_load.value, "--");
  }

  if (data->hasKey("battery.runtime")) {
    lv_label_set_text(s_card_runtime.value,
                       format_runtime((long)data->getFloat("battery.runtime", -1)).c_str());
  } else {
    lv_label_set_text(s_card_runtime.value, "--");
  }

  if (data->hasKey("ups.realpower")) {
    snprintf(buf, sizeof(buf), "%dW", (int)roundf(data->getFloat("ups.realpower")));
    lv_label_set_text(s_card_power.value, buf);
  } else if (data->hasKey("ups.power")) {
    snprintf(buf, sizeof(buf), "%dW", (int)roundf(data->getFloat("ups.power")));
    lv_label_set_text(s_card_power.value, buf);
  } else {
    lv_label_set_text(s_card_power.value, "--");
  }
}
