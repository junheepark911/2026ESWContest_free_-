#include <Arduino.h>
#include "driver/i2c.h"
#include "esp_err.h"
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "drink_images.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// ============================================================
// Cafe Kiosk Portrait V10 Webcam Serial Link
// - 480 x 800 portrait UI, LVGL v8
// - Boot always starts in NORMAL MODE
// - Touch flow: Home(order type only) -> Menu category -> Qty -> Cart -> Payment -> Complete -> Home
// - 노트북의 OpenCV/OpenVINO 프로그램이 고령자를 판정함
// - 노트북과 ESP32-S3를 USB 시리얼(115200 baud)로 연결함
// - Python에서 'S'를 보내면 Simple Mode 전환 팝업을 표시함
// - 'N'을 보내면 Normal Mode 홈으로 복귀함
// - 기존 VL53L0X 거리 센서는 비활성화함
// ============================================================

#define SENSOR_TEST_BY_SERIAL 1
#define USE_VL53L0X_SENSOR 0

// Waveshare ESP32-S3-Touch-LCD-7의 외부 I2C 커넥터는 GPIO8(SDA), GPIO9(SCL) 버스임.
// 이 버스는 터치/IO 확장칩과 공유되므로 Arduino Wire/Adafruit 방식이 아니라
// 보드 라이브러리가 이미 설치한 legacy I2C 드라이버를 그대로 사용함. 새 I2C driver_ng / Wire.begin 사용 금지.
#define VL53_SDA_PIN 8
#define VL53_SCL_PIN 9
#define VL53_I2C_PORT I2C_NUM_0
#define VL53_ADDR 0x29
#define SIMPLE_TRIGGER_MM 150
#define SENSOR_READ_INTERVAL_MS 300
#define SENSOR_POPUP_COOLDOWN_MS 7000

// Font: 네 LVGL 설정에서 lv_font_montserrat_26만 켜져 있어서 이것만 사용함.
#define FONT_MAIN (&lv_font_montserrat_26)
#define FONT_SMALL LV_FONT_DEFAULT

static const int SCREEN_W = 480;
static const int SCREEN_H = 800;

// Colors
static const uint32_t C_BG        = 0xFBF6EE;
static const uint32_t C_CARD      = 0xFFFFFF;
static const uint32_t C_LINE      = 0xE7DCCA;
static const uint32_t C_TEXT      = 0x1F1A16;
static const uint32_t C_MUTED     = 0x7B6F63;
static const uint32_t C_BROWN     = 0x5A3317;
static const uint32_t C_DARK      = 0x181512;
static const uint32_t C_LIGHT     = 0xEFE4D4;
static const uint32_t C_RED       = 0xD71920;
static const uint32_t C_GREEN     = 0x48B75A;
static const uint32_t C_BLUE      = 0x2D6CDF;

// ===== App state =====
enum AppMode { MODE_NORMAL, MODE_SIMPLE };
static AppMode currentMode = MODE_NORMAL;
static bool orderToGo = false;

struct MenuItem {
    const char *name;
    int price;
    const char *category;
    bool best;
};

static MenuItem menuItems[] = {
    {"Americano",       4000, "Coffee", true},
    {"Cafe Latte",      4500, "Coffee", true},
    {"Vanilla Latte",   4800, "Coffee", true},
    {"Cappuccino",      4500, "Coffee", false},
    {"Caramel Macchiato", 4800, "Coffee", true},
    {"Mocha",           4800, "Coffee", false},
    {"Espresso",        3000, "Coffee", false},
    {"Lemon Ade",       4500, "Ade", true},
    {"Grapefruit Ade",  4800, "Ade", false},
    {"Blueberry Ade",   4800, "Ade", false},
    {"Peach Ice Tea",   3800, "Tea", true},
    {"Green Tea",       4000, "Tea", false},
    {"Milk Tea",        4500, "Tea", false},
    {"Choco Shake",     5000, "Shake", true},
    {"Strawberry Shake", 5200, "Shake", false},
    {"Vanilla Shake",   5000, "Shake", false}
};
static const int MENU_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);

static int cartQty[MENU_COUNT] = {0};
static int selectedIndex = 0;
static int selectedQty = 1;
static int completeCountdown = 5;
static lv_timer_t *completeTimer = nullptr;
static lv_obj_t *qtyLabel = nullptr;
static lv_obj_t *priceTotalLabel = nullptr;
static lv_obj_t *cartBarLabel = nullptr;
static lv_obj_t *popupObj = nullptr;

#if USE_VL53L0X_SENSOR
static bool vl53Ready = false;
static uint8_t vl53StopVariable = 0;
static unsigned long lastSensorReadMs = 0;
static unsigned long lastPopupMs = 0;
static uint8_t closeDetectCount = 0;
#endif

// ===== Forward declarations =====
static void show_home();
static void show_menu(const char *category);
static void show_detail(int index);
static void show_cart();
static void show_payment();
static void show_payment_progress();
static void show_complete();
static void show_simple_popup();
static void show_simple_home();
static void show_simple_detail(int index);
static void reset_to_normal_home();
#if USE_VL53L0X_SENSOR
static void init_vl53l0x_sensor();
static void poll_vl53l0x_sensor();
#endif

// ===== Utility =====
static int cart_count()
{
    int n = 0;
    for (int i = 0; i < MENU_COUNT; i++) n += cartQty[i];
    return n;
}

static int cart_total()
{
    int total = 0;
    for (int i = 0; i < MENU_COUNT; i++) total += cartQty[i] * menuItems[i].price;
    return total;
}

static void clear_cart()
{
    for (int i = 0; i < MENU_COUNT; i++) cartQty[i] = 0;
}

static void money(char *buf, size_t len, int price)
{
    snprintf(buf, len, "%d won", price);
}

static void money_total(char *buf, size_t len, int price)
{
    snprintf(buf, len, "%d won", price);
}

static void load_new_screen(lv_obj_t *screen)
{
    if (completeTimer != nullptr) {
        lv_timer_del(completeTimer);
        completeTimer = nullptr;
    }

    lv_obj_t *old = lv_scr_act();
    lv_scr_load(screen);
    if (old != nullptr && old != screen) {
        lv_obj_del_async(old);
    }
}

static lv_obj_t *base_screen(uint32_t color)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(screen, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 10, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// 메뉴 번호에 맞는 음료 이미지를 생성합니다.
// 128px 원본 기준: 320=160px, 240=120px, 160=80px, 140=70px
static lv_obj_t *make_drink_image(lv_obj_t *parent, int menuIndex, uint16_t zoom)
{
    if (menuIndex < 0 || menuIndex >= MENU_COUNT) return nullptr;

    lv_obj_t *image = lv_img_create(parent);
    lv_img_set_src(image, drinkImages[menuIndex]);
    lv_img_set_zoom(image, zoom);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, int w, int h, uint32_t bg, uint32_t fg)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FONT_MAIN, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(fg), 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *make_top_icon_btn(lv_obj_t *parent, const char *text, int x)
{
    lv_obj_t *btn = make_btn(parent, text, 64, 52, C_BG, C_TEXT);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, 20);
    return btn;
}

static void header(lv_obj_t *screen, const char *title, bool back, bool home, bool simple)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, SCREEN_W, 88);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(simple ? 0xFFFDF8 : C_BG), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    if (back) {
        lv_obj_t *b = make_top_icon_btn(bar, "<", 16);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            LV_UNUSED(e);
            if (currentMode == MODE_SIMPLE) show_simple_home();
            else show_home();
        }, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *t = make_label(bar, title, FONT_MAIN, C_TEXT);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 4);

    if (home) {
        lv_obj_t *h = make_top_icon_btn(bar, "Home", SCREEN_W - 88);
        lv_obj_add_event_cb(h, [](lv_event_t *e) {
            LV_UNUSED(e);
            if (currentMode == MODE_SIMPLE) show_simple_home();
            else show_home();
        }, LV_EVENT_CLICKED, NULL);
    }
}

static void bottom_cart_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, SCREEN_W, 62);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_BROWN), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    char buf[96];
    snprintf(buf, sizeof(buf), "Cart %d   |   %d won", cart_count(), cart_total());
    cartBarLabel = make_label(bar, buf, FONT_MAIN, 0xFFFFFF);
    lv_obj_align(cartBarLabel, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, [](lv_event_t *e) {
        LV_UNUSED(e);
        show_cart();
    }, LV_EVENT_CLICKED, NULL);
}

static void update_qty_labels()
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", selectedQty);
    if (qtyLabel) lv_label_set_text(qtyLabel, buf);

    if (priceTotalLabel) {
        snprintf(buf, sizeof(buf), "%d won", selectedQty * menuItems[selectedIndex].price);
        lv_label_set_text(priceTotalLabel, buf);
    }
}

static void add_selected_to_cart()
{
    if (selectedIndex < 0 || selectedIndex >= MENU_COUNT) return;
    cartQty[selectedIndex] += selectedQty;
    selectedQty = 1;
}

// ===== Event callbacks =====
static void order_type_cb(lv_event_t *e)
{
    bool *toGoPtr = (bool *)lv_event_get_user_data(e);
    orderToGo = *toGoPtr;
    // 첫 화면에서는 매장/포장만 선택하고, 선택 후 메뉴 카테고리 화면으로 이동
    show_menu("Coffee");
}

static void category_cb(lv_event_t *e)
{
    const char *cat = (const char *)lv_event_get_user_data(e);
    show_menu(cat);
}

static void item_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (currentMode == MODE_SIMPLE) show_simple_detail(index);
    else show_detail(index);
}

static void minus_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (selectedQty > 1) selectedQty--;
    update_qty_labels();
}

static void plus_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (selectedQty < 9) selectedQty++;
    update_qty_labels();
}

static void add_cart_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    add_selected_to_cart();
    if (currentMode == MODE_SIMPLE) show_simple_home();
    else show_cart();
}

static void direct_pay_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    add_selected_to_cart();
    show_payment();
}

static void normal_mode_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    currentMode = MODE_NORMAL;
    show_home();
}

// ===== Screens =====
static void show_home()
{
    // 첫 화면: 매장식사 / 포장 선택만 보여줌
    currentMode = MODE_NORMAL;
    lv_obj_t *screen = base_screen(C_BG);

    lv_obj_t *logo = make_label(screen, "CAFE KIOSK", FONT_MAIN, C_TEXT);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 72);

    lv_obj_t *mode = make_label(screen, "NORMAL MODE", FONT_SMALL, C_MUTED);
    lv_obj_align(mode, LV_ALIGN_TOP_MID, 0, 116);

    lv_obj_t *title = make_label(screen, "Choose Order Type", FONT_MAIN, C_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 170);

    lv_obj_t *guide = make_label(screen, "Select Eat In or Take Out first", FONT_SMALL, C_MUTED);
    lv_obj_align(guide, LV_ALIGN_TOP_MID, 0, 214);

    static bool eatInVal = false;
    static bool toGoVal = true;

    lv_obj_t *eat = make_btn(screen, "Eat In\nFor Here", 360, 150, C_CARD, C_TEXT);
    lv_obj_align(eat, LV_ALIGN_TOP_MID, 0, 290);
    lv_obj_add_event_cb(eat, order_type_cb, LV_EVENT_CLICKED, &eatInVal);

    lv_obj_t *go = make_btn(screen, "Take Out\nTo Go", 360, 150, C_DARK, 0xFFFFFF);
    lv_obj_align(go, LV_ALIGN_TOP_MID, 0, 470);
    lv_obj_add_event_cb(go, order_type_cb, LV_EVENT_CLICKED, &toGoVal);

    lv_obj_t *hint = make_label(screen, "Next: menu categories", FONT_SMALL, C_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -56);

    load_new_screen(screen);
}

static void category_tab_cb(lv_event_t *e)
{
    const char *cat = (const char *)lv_event_get_user_data(e);
    show_menu(cat);
}

static void make_category_tabs(lv_obj_t *screen, const char *selected)
{
    const char *cats[] = {"Coffee", "Ade", "Tea", "Shake"};
    for (int i = 0; i < 4; i++) {
        bool active = strcmp(cats[i], selected) == 0;
        lv_obj_t *b = make_btn(screen, cats[i], 104, 58,
                               active ? C_DARK : C_CARD,
                               active ? 0xFFFFFF : C_TEXT);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 18 + i * 113, 100);
        lv_obj_add_event_cb(b, category_tab_cb, LV_EVENT_CLICKED, (void *)cats[i]);
    }
}

static void show_menu(const char *category)
{
    // 두 번째 화면: 상단 메뉴 카테고리 + 선택 가능한 메뉴 리스트
    lv_obj_t *screen = base_screen(C_BG);
    header(screen, "Menu", true, true, false);

    lv_obj_t *orderType = make_label(screen, orderToGo ? "Take Out selected" : "Eat In selected", FONT_SMALL, C_MUTED);
    lv_obj_align(orderType, LV_ALIGN_TOP_MID, 0, 74);

    make_category_tabs(screen, category);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_size(list, 432, 520);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int y = 0;
    for (int i = 0; i < MENU_COUNT; i++) {
        if (strcmp(menuItems[i].category, category) != 0) continue;

        lv_obj_t *card = lv_btn_create(list);
        lv_obj_set_size(card, 416, 92);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_add_event_cb(card, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *pic = make_drink_image(card, i, 140);
        lv_obj_align(pic, LV_ALIGN_LEFT_MID, 12, 0);

        lv_obj_t *name = make_label(card, menuItems[i].name, FONT_SMALL, C_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 86, -14);

        char price[32];
        money(price, sizeof(price), menuItems[i].price);
        lv_obj_t *p = make_label(card, price, FONT_SMALL, C_MUTED);
        lv_obj_align(p, LV_ALIGN_LEFT_MID, 86, 16);

        lv_obj_t *arrow = make_label(card, ">", FONT_MAIN, C_TEXT);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);

        y += 102;
    }

    bottom_cart_bar(screen);
    load_new_screen(screen);
}

static void show_detail(int index)
{
    if (index < 0 || index >= MENU_COUNT) return;
    currentMode = MODE_NORMAL;
    selectedIndex = index;
    selectedQty = 1;
    qtyLabel = nullptr;
    priceTotalLabel = nullptr;

    lv_obj_t *screen = base_screen(C_BG);
    header(screen, "Quantity", true, true, false);

    lv_obj_t *product = make_card(screen, 320, 280);
    lv_obj_align(product, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_bg_color(product, lv_color_hex(0xF4E8D8), 0);

    lv_obj_t *cup = make_drink_image(product, index, 320);
    lv_obj_align(cup, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t *name = make_label(screen, menuItems[index].name, FONT_MAIN, C_TEXT);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 420);

    char price[40];
    money(price, sizeof(price), menuItems[index].price);
    lv_obj_t *p = make_label(screen, price, FONT_MAIN, C_TEXT);
    lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 462);

    lv_obj_t *qtxt = make_label(screen, "Quantity", FONT_SMALL, C_MUTED);
    lv_obj_align(qtxt, LV_ALIGN_TOP_MID, 0, 525);

    lv_obj_t *minus = make_btn(screen, "-", 82, 76, C_DARK, 0xFFFFFF);
    lv_obj_align(minus, LV_ALIGN_TOP_LEFT, 70, 560);
    lv_obj_add_event_cb(minus, minus_cb, LV_EVENT_CLICKED, NULL);

    qtyLabel = make_label(screen, "1", FONT_MAIN, C_TEXT);
    lv_obj_set_style_text_font(qtyLabel, FONT_MAIN, 0);
    lv_obj_align(qtyLabel, LV_ALIGN_TOP_MID, 0, 578);

    lv_obj_t *plus = make_btn(screen, "+", 82, 76, C_DARK, 0xFFFFFF);
    lv_obj_align(plus, LV_ALIGN_TOP_RIGHT, -70, 560);
    lv_obj_add_event_cb(plus, plus_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *add = make_btn(screen, "Add to Cart", 420, 68, C_DARK, 0xFFFFFF);
    lv_obj_align(add, LV_ALIGN_TOP_MID, 0, 655);
    lv_obj_add_event_cb(add, add_cart_cb, LV_EVENT_CLICKED, NULL);

    bottom_cart_bar(screen);
    update_qty_labels();
    load_new_screen(screen);
}

static void show_cart()
{
    lv_obj_t *screen = base_screen(C_BG);
    header(screen, "Cart", true, true, false);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_size(list, 432, 430);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 105);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int y = 0;
    int hasItem = 0;
    for (int i = 0; i < MENU_COUNT; i++) {
        if (cartQty[i] <= 0) continue;
        hasItem = 1;
        lv_obj_t *card = make_card(list, 416, 104);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);

        lv_obj_t *pic = make_drink_image(card, i, 140);
        lv_obj_align(pic, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *name = make_label(card, menuItems[i].name, FONT_SMALL, C_TEXT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 85, 12);

        char line[80];
        snprintf(line, sizeof(line), "%d won  x %d", menuItems[i].price, cartQty[i]);
        lv_obj_t *p = make_label(card, line, FONT_SMALL, C_MUTED);
        lv_obj_align(p, LV_ALIGN_TOP_LEFT, 85, 50);

        y += 116;
    }

    if (!hasItem) {
        lv_obj_t *empty = make_label(list, "Cart is empty\nChoose a menu first", FONT_MAIN, C_MUTED);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 120);
    }

    lv_obj_t *summary = make_card(screen, 480, 145);
    lv_obj_align(summary, LV_ALIGN_BOTTOM_MID, 0, -118);
    lv_obj_set_style_radius(summary, 0, 0);

    lv_obj_t *a = make_label(summary, "Order amount", FONT_SMALL, C_TEXT);
    lv_obj_align(a, LV_ALIGN_TOP_LEFT, 26, 18);

    char totalBuf[40];
    money_total(totalBuf, sizeof(totalBuf), cart_total());
    lv_obj_t *total = make_label(summary, totalBuf, FONT_MAIN, C_TEXT);
    lv_obj_align(total, LV_ALIGN_TOP_RIGHT, -26, 12);

    lv_obj_t *b = make_label(summary, "Discount", FONT_SMALL, C_MUTED);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, 26, 60);
    lv_obj_t *discount = make_label(summary, "0 won", FONT_SMALL, C_RED);
    lv_obj_align(discount, LV_ALIGN_TOP_RIGHT, -26, 60);

    lv_obj_t *c = make_label(summary, "Total payment", FONT_SMALL, C_TEXT);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 26, 100);
    lv_obj_t *payTotal = make_label(summary, totalBuf, FONT_MAIN, C_RED);
    lv_obj_align(payTotal, LV_ALIGN_TOP_RIGHT, -26, 92);

    lv_obj_t *addMenu = make_btn(screen, "Add Menu", 198, 72, C_LIGHT, C_TEXT);
    lv_obj_align(addMenu, LV_ALIGN_BOTTOM_LEFT, 24, -26);
    lv_obj_add_event_cb(addMenu, [](lv_event_t *e) {
        LV_UNUSED(e);
        if (currentMode == MODE_SIMPLE) show_simple_home();
        else show_menu("Coffee");
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pay = make_btn(screen, "Pay", 198, 72, C_DARK, 0xFFFFFF);
    lv_obj_align(pay, LV_ALIGN_BOTTOM_RIGHT, -24, -26);
    lv_obj_add_event_cb(pay, [](lv_event_t *e) {
        LV_UNUSED(e);
        if (cart_count() > 0) show_payment();
    }, LV_EVENT_CLICKED, NULL);

    load_new_screen(screen);
}

static void show_payment()
{
    lv_obj_t *screen = base_screen(C_BG);
    header(screen, "Payment", true, true, false);

    lv_obj_t *guide = make_label(screen, "Choose payment method", FONT_SMALL, C_TEXT);
    lv_obj_align(guide, LV_ALIGN_TOP_MID, 0, 130);

    const char *methods[] = {"Credit Card", "Easy Pay", "Cash", "Coupon"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *m = make_btn(screen, methods[i], 190, 138, C_CARD, C_TEXT);
        lv_obj_align(m, LV_ALIGN_TOP_LEFT, 38 + (i % 2) * 214, 180 + (i / 2) * 160);
        lv_obj_add_event_cb(m, [](lv_event_t *e) {
            LV_UNUSED(e);
            show_payment_progress();
        }, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *sum = make_label(screen, "Total payment", FONT_SMALL, C_TEXT);
    lv_obj_align(sum, LV_ALIGN_BOTTOM_MID, 0, -155);

    char total[40];
    money_total(total, sizeof(total), cart_total());
    lv_obj_t *amount = make_label(screen, total, FONT_MAIN, C_RED);
    lv_obj_align(amount, LV_ALIGN_BOTTOM_MID, 0, -100);

    load_new_screen(screen);
}

static void show_payment_progress()
{
    lv_obj_t *screen = base_screen(C_BG);
    header(screen, "Processing", false, true, false);

    lv_obj_t *circle = make_card(screen, 230, 230);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_radius(circle, 115, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xEFE4D4), 0);
    lv_obj_t *card = make_label(circle, "[ Card ]", FONT_MAIN, C_TEXT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *msg = make_label(screen, "Insert card or\ncontinue easy payment", FONT_MAIN, C_TEXT);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 455);

    char total[40];
    money_total(total, sizeof(total), cart_total());
    lv_obj_t *amount = make_label(screen, total, FONT_MAIN, C_RED);
    lv_obj_align(amount, LV_ALIGN_TOP_MID, 0, 610);

    lv_obj_t *done = make_btn(screen, "Payment Complete", 420, 78, C_DARK, 0xFFFFFF);
    lv_obj_align(done, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_add_event_cb(done, [](lv_event_t *e) {
        LV_UNUSED(e);
        show_complete();
    }, LV_EVENT_CLICKED, NULL);

    load_new_screen(screen);
}

static void complete_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *label = (lv_obj_t *)timer->user_data;
    completeCountdown--;

    if (completeCountdown <= 0) {
        completeTimer = nullptr;
        lv_timer_del(timer);
        reset_to_normal_home();
        return;
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", completeCountdown);
    lv_label_set_text(label, buf);
}

static void show_complete()
{
    // 결제 완료 신호를 노트북 Python 프로그램으로 전송
    // Python이 이 신호를 받아 Arduino USB 포트로 H를 중계함
    Serial.println("PAYMENT_COMPLETE");

    lv_obj_t *screen = base_screen(C_BG);
    header(screen, "Order Complete", false, true, false);

    lv_obj_t *check = make_card(screen, 170, 170);
    lv_obj_align(check, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_radius(check, 85, 0);
    lv_obj_set_style_bg_color(check, lv_color_hex(C_GREEN), 0);
    lv_obj_t *v = make_label(check, "OK", FONT_MAIN, 0xFFFFFF);
    lv_obj_align(v, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *thanks = make_label(screen, "Thank you!", FONT_MAIN, C_TEXT);
    lv_obj_align(thanks, LV_ALIGN_TOP_MID, 0, 410);

    lv_obj_t *msg = make_label(screen, "Order has been completed.\nReturn to home automatically.", FONT_SMALL, C_MUTED);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 462);

    completeCountdown = 5;
    lv_obj_t *circle = make_card(screen, 96, 96);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 585);
    lv_obj_set_style_radius(circle, 48, 0);
    lv_obj_t *count = make_label(circle, "5", FONT_MAIN, C_TEXT);
    lv_obj_align(count, LV_ALIGN_CENTER, 0, 0);

    load_new_screen(screen);
    completeTimer = lv_timer_create(complete_timer_cb, 1000, count);
}

static void reset_to_normal_home()
{
    clear_cart();
    selectedQty = 1;
    selectedIndex = 0;
    currentMode = MODE_NORMAL;
    show_home();
}

// ===== Simple mode =====
static void show_simple_popup()
{
    if (currentMode == MODE_SIMPLE || popupObj != nullptr) return;
#if USE_VL53L0X_SENSOR
    lastPopupMs = millis();
#endif

    popupObj = lv_obj_create(lv_layer_top());
    lv_obj_set_size(popupObj, 420, 260);
    lv_obj_center(popupObj);
    lv_obj_set_style_bg_color(popupObj, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(popupObj, lv_color_hex(C_BROWN), 0);
    lv_obj_set_style_border_width(popupObj, 2, 0);
    lv_obj_set_style_radius(popupObj, 14, 0);
    lv_obj_clear_flag(popupObj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *msg = make_label(popupObj, "Switch to\nSimple Mode?", FONT_MAIN, C_TEXT);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *sub = make_label(popupObj, "Senior user detected by camera", FONT_SMALL, C_MUTED);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 118);

    lv_obj_t *cancel = make_btn(popupObj, "Cancel", 170, 72, C_LIGHT, C_TEXT);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 24, -24);
    lv_obj_add_event_cb(cancel, [](lv_event_t *e) {
        LV_UNUSED(e);
        if (popupObj) {
            lv_obj_del(popupObj);
            popupObj = nullptr;
        }
#if USE_VL53L0X_SENSOR
        lastPopupMs = millis();
        closeDetectCount = 0;
#endif
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ok = make_btn(popupObj, "OK", 170, 72, C_BROWN, 0xFFFFFF);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, -24, -24);
    lv_obj_add_event_cb(ok, [](lv_event_t *e) {
        LV_UNUSED(e);
        if (popupObj) {
            lv_obj_del(popupObj);
            popupObj = nullptr;
        }
#if USE_VL53L0X_SENSOR
        lastPopupMs = millis();
        closeDetectCount = 0;
#endif
        show_simple_home();
    }, LV_EVENT_CLICKED, NULL);
}

static void simple_header(lv_obj_t *screen, const char *title)
{
    lv_obj_t *normal = make_btn(screen, "< Normal", 138, 52, C_CARD, C_TEXT);
    lv_obj_align(normal, LV_ALIGN_TOP_LEFT, 18, 20);
    lv_obj_add_event_cb(normal, normal_mode_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t = make_label(screen, title, FONT_MAIN, C_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 86);

    lv_obj_t *home = make_top_icon_btn(screen, "Home", SCREEN_W - 88);
    lv_obj_add_event_cb(home, [](lv_event_t *e) {
        LV_UNUSED(e);
        show_simple_home();
    }, LV_EVENT_CLICKED, NULL);
}

static void show_simple_home()
{
    currentMode = MODE_SIMPLE;
    lv_obj_t *screen = base_screen(0xFFFDF8);
    simple_header(screen, "SIMPLE MODE");

    lv_obj_t *guide = make_label(screen, "Choose one menu", FONT_SMALL, C_MUTED);
    lv_obj_align(guide, LV_ALIGN_TOP_MID, 0, 132);

    int idx[] = {0, 1, 3, 5};
    int sx[] = {30, 250, 30, 250};
    int sy[] = {180, 180, 405, 405};

    for (int i = 0; i < 4; i++) {
        int mi = idx[i];
        lv_obj_t *card = lv_btn_create(screen);
        lv_obj_set_size(card, 200, 198);
        lv_obj_align(card, LV_ALIGN_TOP_LEFT, sx[i], sy[i]);
        lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_add_event_cb(card, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)mi);

        lv_obj_t *img = make_drink_image(card, mi, 160);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *name = make_label(card, menuItems[mi].name, FONT_MAIN, C_TEXT);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -28);
    }

    char cartInfo[80];
    snprintf(cartInfo, sizeof(cartInfo), "Order items: %d", cart_count());
    lv_obj_t *ci = make_label(screen, cartInfo, FONT_SMALL, C_TEXT);
    lv_obj_align(ci, LV_ALIGN_BOTTOM_LEFT, 30, -110);

    lv_obj_t *pay = make_btn(screen, "Pay", 420, 84, C_DARK, 0xFFFFFF);
    lv_obj_align(pay, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(pay, [](lv_event_t *e) {
        LV_UNUSED(e);
        if (cart_count() > 0) show_payment();
    }, LV_EVENT_CLICKED, NULL);

    load_new_screen(screen);
}

static void show_simple_detail(int index)
{
    if (index < 0 || index >= MENU_COUNT) return;
    currentMode = MODE_SIMPLE;
    selectedIndex = index;
    selectedQty = 1;
    qtyLabel = nullptr;
    priceTotalLabel = nullptr;

    lv_obj_t *screen = base_screen(0xFFFDF8);
    simple_header(screen, "Select Quantity");

    lv_obj_t *drink = make_drink_image(screen, index, 240);
    lv_obj_align(drink, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *name = make_label(screen, menuItems[index].name, FONT_MAIN, C_TEXT);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 270);

    char price[40];
    money(price, sizeof(price), menuItems[index].price);
    lv_obj_t *p = make_label(screen, price, FONT_MAIN, C_TEXT);
    lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 312);

    lv_obj_t *minus = make_btn(screen, "-", 126, 118, C_DARK, 0xFFFFFF);
    lv_obj_align(minus, LV_ALIGN_TOP_LEFT, 36, 335);
    lv_obj_add_event_cb(minus, minus_cb, LV_EVENT_CLICKED, NULL);

    qtyLabel = make_label(screen, "1", FONT_MAIN, C_TEXT);
    lv_obj_align(qtyLabel, LV_ALIGN_TOP_MID, 0, 374);

    lv_obj_t *plus = make_btn(screen, "+", 126, 118, C_DARK, 0xFFFFFF);
    lv_obj_align(plus, LV_ALIGN_TOP_RIGHT, -36, 335);
    lv_obj_add_event_cb(plus, plus_cb, LV_EVENT_CLICKED, NULL);

    priceTotalLabel = make_label(screen, "", FONT_MAIN, C_RED);
    lv_obj_align(priceTotalLabel, LV_ALIGN_TOP_MID, 0, 500);

    lv_obj_t *add = make_btn(screen, "Add to Cart", 420, 88, C_LIGHT, C_TEXT);
    lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -124);
    lv_obj_add_event_cb(add, add_cart_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pay = make_btn(screen, "Pay Now", 420, 88, C_DARK, 0xFFFFFF);
    lv_obj_align(pay, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(pay, direct_pay_cb, LV_EVENT_CLICKED, NULL);

    update_qty_labels();
    load_new_screen(screen);
}


// ===== VL53L0X distance sensor using already-installed legacy I2C driver =====
#if USE_VL53L0X_SENSOR

// 절대 Wire.begin(), Adafruit_VL53L0X, driver/i2c_master.h를 쓰지 않음.
// ESP32_Display_Panel이 이미 I2C_NUM_0을 초기화했으므로 여기서는 읽기/쓰기만 한다.

// VL53L0X register addresses used here
#define VL53_REG_SYSRANGE_START                         0x00
#define VL53_REG_SYSTEM_SEQUENCE_CONFIG                 0x01
#define VL53_REG_SYSTEM_INTERRUPT_CONFIG_GPIO           0x0A
#define VL53_REG_SYSTEM_INTERRUPT_CLEAR                 0x0B
#define VL53_REG_RESULT_INTERRUPT_STATUS                0x13
#define VL53_REG_RESULT_RANGE_STATUS                    0x14
#define VL53_REG_MSRC_CONFIG_CONTROL                    0x60
#define VL53_REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0       0xB0
#define VL53_REG_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD    0x4E
#define VL53_REG_DYNAMIC_SPAD_REF_EN_START_OFFSET       0x4F
#define VL53_REG_GLOBAL_CONFIG_REF_EN_START_SELECT      0xB6
#define VL53_REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT 0x44
#define VL53_REG_GPIO_HV_MUX_ACTIVE_HIGH                0x84
#define VL53_REG_VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV     0x89

static bool vl53_write8(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    esp_err_t err = i2c_master_write_to_device(VL53_I2C_PORT, VL53_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

static bool vl53_write16(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    esp_err_t err = i2c_master_write_to_device(VL53_I2C_PORT, VL53_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

static bool vl53_write_multi(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!data || len > 31) return false;
    uint8_t buf[32];
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    esp_err_t err = i2c_master_write_to_device(VL53_I2C_PORT, VL53_ADDR, buf, len + 1, pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

static bool vl53_read8(uint8_t reg, uint8_t *value)
{
    if (!value) return false;
    esp_err_t err = i2c_master_write_read_device(VL53_I2C_PORT, VL53_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

static bool vl53_read16(uint8_t reg, uint16_t *value)
{
    if (!value) return false;
    uint8_t data[2] = {0, 0};
    esp_err_t err = i2c_master_write_read_device(VL53_I2C_PORT, VL53_ADDR, &reg, 1, data, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return false;
    *value = ((uint16_t)data[0] << 8) | data[1];
    return true;
}

static bool vl53_read_multi(uint8_t reg, uint8_t *data, size_t len)
{
    if (!data) return false;
    esp_err_t err = i2c_master_write_read_device(VL53_I2C_PORT, VL53_ADDR, &reg, 1, data, len, pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

static bool vl53_probe()
{
    uint8_t id = 0;
    uint8_t reg = 0xC0;
    esp_err_t err = i2c_master_write_read_device(VL53_I2C_PORT, VL53_ADDR, &reg, 1, &id, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        Serial.printf("VL53L0X probe failed on I2C_NUM_0 addr 0x29: %s\n", esp_err_to_name(err));
        return false;
    }
    Serial.printf("VL53L0X ID reg 0xC0 = 0x%02X\n", id);
    return true;
}

static bool vl53_set_signal_rate_limit(float limit_mcps)
{
    if (limit_mcps < 0 || limit_mcps > 511.99f) return false;
    uint16_t value = (uint16_t)(limit_mcps * (1 << 7));
    return vl53_write16(VL53_REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT, value);
}

static bool vl53_get_spad_info(uint8_t *count, bool *type_is_aperture)
{
    uint8_t tmp = 0;
    vl53_write8(0x80, 0x01);
    vl53_write8(0xFF, 0x01);
    vl53_write8(0x00, 0x00);
    vl53_write8(0xFF, 0x06);
    vl53_read8(0x83, &tmp);
    vl53_write8(0x83, tmp | 0x04);
    vl53_write8(0xFF, 0x07);
    vl53_write8(0x81, 0x01);
    vl53_write8(0x80, 0x01);
    vl53_write8(0x94, 0x6b);
    vl53_write8(0x83, 0x00);

    uint32_t start = millis();
    do {
        if (!vl53_read8(0x83, &tmp)) return false;
        if (millis() - start > 500) return false;
        delay(2);
    } while (tmp == 0x00);

    vl53_write8(0x83, 0x01);
    uint8_t value = 0;
    if (!vl53_read8(0x92, &value)) return false;
    *count = value & 0x7F;
    *type_is_aperture = (value >> 7) & 0x01;

    vl53_write8(0x81, 0x00);
    vl53_write8(0xFF, 0x06);
    vl53_read8(0x83, &tmp);
    vl53_write8(0x83, tmp & ~0x04);
    vl53_write8(0xFF, 0x01);
    vl53_write8(0x00, 0x01);
    vl53_write8(0xFF, 0x00);
    vl53_write8(0x80, 0x00);
    return true;
}

static bool vl53_perform_single_ref_calibration(uint8_t vhv_init_byte)
{
    vl53_write8(VL53_REG_SYSRANGE_START, 0x01 | vhv_init_byte);
    uint32_t start = millis();
    uint8_t status = 0;
    do {
        if (!vl53_read8(VL53_REG_RESULT_INTERRUPT_STATUS, &status)) return false;
        if (millis() - start > 1000) return false;
        delay(2);
    } while ((status & 0x07) == 0);

    vl53_write8(VL53_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    vl53_write8(VL53_REG_SYSRANGE_START, 0x00);
    return true;
}

static bool vl53_apply_default_tuning()
{
    const uint8_t seq[][2] = {
        {0xFF,0x01},{0x00,0x00},{0xFF,0x00},{0x09,0x00},{0x10,0x00},{0x11,0x00},
        {0x24,0x01},{0x25,0xFF},{0x75,0x00},{0xFF,0x01},{0x4E,0x2C},{0x48,0x00},
        {0x30,0x20},{0xFF,0x00},{0x30,0x09},{0x54,0x00},{0x31,0x04},{0x32,0x03},
        {0x40,0x83},{0x46,0x25},{0x60,0x00},{0x27,0x00},{0x50,0x06},{0x51,0x00},
        {0x52,0x96},{0x56,0x08},{0x57,0x30},{0x61,0x00},{0x62,0x00},{0x64,0x00},
        {0x65,0x00},{0x66,0xA0},{0xFF,0x01},{0x22,0x32},{0x47,0x14},{0x49,0xFF},
        {0x4A,0x00},{0xFF,0x00},{0x7A,0x0A},{0x7B,0x00},{0x78,0x21},{0xFF,0x01},
        {0x23,0x34},{0x42,0x00},{0x44,0xFF},{0x45,0x26},{0x46,0x05},{0x40,0x40},
        {0x0E,0x06},{0x20,0x1A},{0x43,0x40},{0xFF,0x00},{0x34,0x03},{0x35,0x44},
        {0xFF,0x01},{0x31,0x04},{0x4B,0x09},{0x4C,0x05},{0x4D,0x04},{0xFF,0x00},
        {0x44,0x00},{0x45,0x20},{0x47,0x08},{0x48,0x28},{0x67,0x00},{0x70,0x04},
        {0x71,0x01},{0x72,0xFE},{0x76,0x00},{0x77,0x00},{0xFF,0x01},{0x0D,0x01},
        {0xFF,0x00},{0x80,0x01},{0x01,0xF8},{0xFF,0x01},{0x8E,0x01},{0x00,0x01},
        {0xFF,0x00},{0x80,0x00}
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        if (!vl53_write8(seq[i][0], seq[i][1])) return false;
    }
    return true;
}

static bool vl53_raw_init()
{
    uint8_t tmp = 0;
    if (!vl53_probe()) return false;

    if (vl53_read8(VL53_REG_VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV, &tmp)) {
        vl53_write8(VL53_REG_VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV, tmp | 0x01);
    }

    vl53_write8(0x88, 0x00);
    vl53_write8(0x80, 0x01);
    vl53_write8(0xFF, 0x01);
    vl53_write8(0x00, 0x00);
    if (!vl53_read8(0x91, &vl53StopVariable)) return false;
    vl53_write8(0x00, 0x01);
    vl53_write8(0xFF, 0x00);
    vl53_write8(0x80, 0x00);

    if (vl53_read8(VL53_REG_MSRC_CONFIG_CONTROL, &tmp)) {
        vl53_write8(VL53_REG_MSRC_CONFIG_CONTROL, tmp | 0x12);
    }
    vl53_set_signal_rate_limit(0.25f);
    vl53_write8(VL53_REG_SYSTEM_SEQUENCE_CONFIG, 0xFF);

    uint8_t spad_count = 0;
    bool spad_type_is_aperture = false;
    if (!vl53_get_spad_info(&spad_count, &spad_type_is_aperture)) return false;
    Serial.printf("VL53L0X SPAD count=%u aperture=%d\n", spad_count, spad_type_is_aperture ? 1 : 0);

    uint8_t ref_spad_map[6] = {0};
    if (!vl53_read_multi(VL53_REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6)) return false;

    vl53_write8(0xFF, 0x01);
    vl53_write8(VL53_REG_DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00);
    vl53_write8(VL53_REG_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C);
    vl53_write8(0xFF, 0x00);
    vl53_write8(VL53_REG_GLOBAL_CONFIG_REF_EN_START_SELECT, 0xB4);

    uint8_t first_spad_to_enable = spad_type_is_aperture ? 12 : 0;
    uint8_t spads_enabled = 0;
    for (uint8_t i = 0; i < 48; i++) {
        if (i < first_spad_to_enable || spads_enabled == spad_count) {
            ref_spad_map[i / 8] &= ~(1 << (i % 8));
        } else if (ref_spad_map[i / 8] & (1 << (i % 8))) {
            spads_enabled++;
        }
    }
    if (!vl53_write_multi(VL53_REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6)) return false;

    if (!vl53_apply_default_tuning()) return false;

    vl53_write8(VL53_REG_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04);
    if (vl53_read8(VL53_REG_GPIO_HV_MUX_ACTIVE_HIGH, &tmp)) {
        vl53_write8(VL53_REG_GPIO_HV_MUX_ACTIVE_HIGH, tmp & ~0x10);
    }
    vl53_write8(VL53_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);

    vl53_write8(VL53_REG_SYSTEM_SEQUENCE_CONFIG, 0xE8);
    vl53_write8(VL53_REG_SYSTEM_SEQUENCE_CONFIG, 0x01);
    if (!vl53_perform_single_ref_calibration(0x40)) return false;
    vl53_write8(VL53_REG_SYSTEM_SEQUENCE_CONFIG, 0x02);
    if (!vl53_perform_single_ref_calibration(0x00)) return false;
    vl53_write8(VL53_REG_SYSTEM_SEQUENCE_CONFIG, 0xE8);

    return true;
}

static bool vl53_read_distance_mm(uint16_t *distance_mm)
{
    if (!vl53Ready || !distance_mm) return false;

    vl53_write8(0x80, 0x01);
    vl53_write8(0xFF, 0x01);
    vl53_write8(0x00, 0x00);
    vl53_write8(0x91, vl53StopVariable);
    vl53_write8(0x00, 0x01);
    vl53_write8(0xFF, 0x00);
    vl53_write8(0x80, 0x00);

    vl53_write8(VL53_REG_SYSRANGE_START, 0x01);

    uint32_t start = millis();
    uint8_t startReg = 0;
    do {
        if (!vl53_read8(VL53_REG_SYSRANGE_START, &startReg)) return false;
        if (millis() - start > 500) return false;
        delay(2);
    } while (startReg & 0x01);

    start = millis();
    uint8_t interruptStatus = 0;
    do {
        if (!vl53_read8(VL53_REG_RESULT_INTERRUPT_STATUS, &interruptStatus)) return false;
        if (millis() - start > 500) return false;
        delay(2);
    } while ((interruptStatus & 0x07) == 0);

    uint16_t range = 0;
    if (!vl53_read16(VL53_REG_RESULT_RANGE_STATUS + 10, &range)) return false;
    vl53_write8(VL53_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);

    *distance_mm = range;
    return true;
}

static void init_vl53l0x_sensor()
{
    Serial.printf("VL53L0X init using legacy I2C_NUM_0 shared bus SDA=%d SCL=%d addr=0x%02X\n", VL53_SDA_PIN, VL53_SCL_PIN, VL53_ADDR);
    Serial.println("No Wire.begin(), no Adafruit_VL53L0X, no driver/i2c_master.h");

    // 여기서 i2c_driver_install()을 호출하면 안 됨. 보드 라이브러리가 이미 설치한 드라이버를 사용한다.
    if (!vl53_raw_init()) {
        Serial.println("VL53L0X not found or init failed. UI will continue without distance sensor.");
        vl53Ready = false;
        return;
    }

    vl53Ready = true;
    Serial.println("VL53L0X ready. Move within 15cm to show Simple Mode popup.");
}

static void poll_vl53l0x_sensor()
{
    if (!vl53Ready) return;
    if (currentMode == MODE_SIMPLE) return;
    if (popupObj != nullptr) return;
    if (millis() - lastSensorReadMs < SENSOR_READ_INTERVAL_MS) return;
    lastSensorReadMs = millis();

    uint16_t mm = 0;
    if (!vl53_read_distance_mm(&mm)) {
        closeDetectCount = 0;
        return;
    }

    Serial.printf("VL53L0X distance: %u mm\n", mm);

    if (mm > 0 && mm <= SIMPLE_TRIGGER_MM) {
        if (closeDetectCount < 5) closeDetectCount++;
    } else {
        closeDetectCount = 0;
    }

    if (closeDetectCount >= 2 && millis() - lastPopupMs > SENSOR_POPUP_COOLDOWN_MS) {
        closeDetectCount = 0;
        if (lvgl_port_lock(100)) {
            show_simple_popup();
            lvgl_port_unlock();
        }
    }
}
#endif

void setup()
{
    Serial.begin(115200);

    delay(300);
    Serial.println("Cafe Kiosk Portrait V10 Webcam Serial Link start");
    Serial.println("Payment relay ready: ESP32 -> Python -> Arduino");

    Board *board = new Board();
    Serial.println("board init");
    board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif

    Serial.println("board begin");
    if (!board->begin()) {
        Serial.println("board begin failed");
        while (1) delay(1000);
    }

    Serial.println("lvgl init");
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("lvgl init failed");
        while (1) delay(1000);
    }

    if (lvgl_port_lock(-1)) {
        show_home();  // Always start Normal Mode
        lvgl_port_unlock();
    }

    Serial.println("UI loaded. Send S for Simple Mode popup, N for Normal Mode.");

#if USE_VL53L0X_SENSOR
    // 화면을 먼저 띄운 뒤 센서를 붙인다. 센서가 실패해도 화면은 유지됨.
    init_vl53l0x_sensor();
#endif
}

void loop()
{
#if SENSOR_TEST_BY_SERIAL
    // 노트북 Python 프로그램에서 USB 시리얼로 명령을 받음.
    // S: 고령자 감지 -> Simple Mode 확인 팝업 표시
    // N: Normal Mode 홈으로 복귀
    if (Serial.available()) {
        char c = Serial.read();

        if (c == 's' || c == 'S') {
            Serial.println("Webcam command received: SENIOR");

            if (lvgl_port_lock(100)) {
                show_simple_popup();
                lvgl_port_unlock();
            }
        }
        else if (c == 'n' || c == 'N') {
            Serial.println("Webcam command received: NORMAL");

            if (lvgl_port_lock(100)) {
                if (popupObj != nullptr) {
                    lv_obj_del(popupObj);
                    popupObj = nullptr;
                }

                reset_to_normal_home();
                lvgl_port_unlock();
            }
        }
    }
#endif
#if USE_VL53L0X_SENSOR
    poll_vl53l0x_sensor();
#endif
    delay(20);
}
