// main/wifi_provision_html.c —— 表单页常量与结果页拼装。
#include "wifi_provision_html.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_scan_list.h"

// 动态列表要插在这对标记之间。静态页留一对空标记，正好是"没扫到网络"的降级形态。
#define WIFI_SCAN_DATALIST_OPEN "<datalist id=\"nets\">"
#define WIFI_SCAN_DATALIST_END "</datalist>"

// 表单页：只有一个表单、五个输入框、一个按钮。没有 JS、没有外部资源——
// 设备热点上没有外网，任何 CDN 引用都会让页面卡在加载状态。
//
// 这里刻意保留一个空的 <datalist>：静态页是"没有扫到任何网络"时的降级形态，
// 与加扫描之前逐字节等价；扫到网络时由 wifi_provision_html_form 在同一对
// 标记之间填入 <option>，两段拼接共用同一份标记，不会走样。
static const char FORM_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Wi-Fi Setup</title>"
    "<style>body{font-family:sans-serif;margin:24px;background:#101a24;color:#f6f1e3}"
    "h2{font-weight:500;margin:0 0 16px}"
    "label{display:block;margin:16px 0 6px;font-size:14px;color:#93a1ae}"
    "input{width:100%;box-sizing:border-box;padding:12px;font-size:16px;"
    "border:1px solid #93a1ae;border-radius:6px;background:#f6f1e3;color:#101a24}"
    "button{margin-top:24px;width:100%;padding:14px;font-size:16px;"
    "border:0;border-radius:6px;background:#b6412c;color:#f6f1e3}"
    "p{font-size:14px;color:#93a1ae}</style></head><body>"
    "<h2>Connect this device</h2>"
    "<form method=\"post\" action=\"" WIFI_PROV_SAVE_PATH "\">"
    "<label for=\"ssid\">Network name</label>"
    "<input id=\"ssid\" name=\"" WIFI_PROV_FIELD_SSID "\" maxlength=\"32\" list=\"nets\""
    " autocapitalize=\"none\" autocorrect=\"off\" required>"
    "<datalist id=\"nets\">" WIFI_SCAN_DATALIST_END
    "<label for=\"pass\">Password</label>"
    "<input id=\"pass\" name=\"" WIFI_PROV_FIELD_PASS "\" type=\"password\""
    " maxlength=\"64\">"
    "<p>Weather needs a location. Leave both coordinates empty if this network has none.</p>"
    "<label for=\"lat\">Latitude</label>"
    "<input id=\"lat\" name=\"" WIFI_PROV_FIELD_LAT "\" inputmode=\"decimal\""
    " maxlength=\"16\" placeholder=\"39.9042\">"
    "<label for=\"lon\">Longitude</label>"
    "<input id=\"lon\" name=\"" WIFI_PROV_FIELD_LON "\" inputmode=\"decimal\""
    " maxlength=\"16\" placeholder=\"116.4074\">"
    "<label for=\"place\">Location name</label>"
    "<input id=\"place\" name=\"" WIFI_PROV_FIELD_PLACE "\" maxlength=\"23\""
    " placeholder=\"Beijing\">"
    "<button type=\"submit\">Save and restart</button></form>"
    "<p>2.4 GHz networks only. The device restarts after saving.</p>"
    "</body></html>";

const char *wifi_provision_form_page(size_t *len) {
    if (len != NULL) *len = sizeof FORM_PAGE - 1;
    return FORM_PAGE;
}

// 有界写入器：一旦放不下就置 overflow 并停止累加，最后由调用方统一决定"全不写"。
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    bool   overflow;
} writer_t;

static void w_raw(writer_t *w, const char *text, size_t n) {
    if (w->overflow) return;
    if (w->len + n + 1 > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, text, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

static void w_str(writer_t *w, const char *text) {
    if (text == NULL) return;
    w_raw(w, text, strlen(text));
}

// HTML 转义。用户提交的 SSID 会回显到结果页上，不转义就等于把设备的配网页
// 变成一个反射型 XSS 的入口。
static void w_esc(writer_t *w, const char *text) {
    if (text == NULL) return;
    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
        case '&':  w_str(w, "&amp;"); break;
        case '<':  w_str(w, "&lt;"); break;
        case '>':  w_str(w, "&gt;"); break;
        case '"':  w_str(w, "&quot;"); break;
        case '\'': w_str(w, "&#39;"); break;
        default: {
            char one[2] = { *p, '\0' };
            w_str(w, one);
            break;
        }
        }
    }
}

static size_t writer_finish(writer_t *w) {
    if (w->overflow) {
        // 放弃整页，避免把半截 HTML 发给手机。
        if (w->cap > 0) w->buf[0] = '\0';
        return 0;
    }
    return w->len;
}

static void begin_page(writer_t *w, const char *title) {
    w_str(w, "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>");
    w_str(w, title);
    w_str(w, "</title><style>body{font-family:sans-serif;margin:24px;background:#101a24;"
             "color:#f6f1e3}h2{font-weight:500;margin:0 0 12px}p{font-size:14px;"
             "color:#93a1ae}a{color:#f6f1e3}</style></head><body>");
}

static void end_page(writer_t *w) {
    w_str(w, "<p><a href=\"/\">Back</a></p></body></html>");
}

size_t wifi_provision_html_saved(char *buf, size_t cap, const char *ssid) {
    if (buf == NULL || cap == 0) return 0;
    writer_t w = { .buf = buf, .cap = cap, .len = 0, .overflow = false };
    buf[0] = '\0';

    begin_page(&w, "Saved");
    w_str(&w, "<h2>Saved. Restarting...</h2><p>");
    w_esc(&w, (ssid != NULL && ssid[0] != '\0') ? ssid : "Your network");
    w_str(&w, "</p><p>The device will reboot and join this network. "
              "You can close this page.</p></body></html>");
    return writer_finish(&w);
}

size_t wifi_provision_html_error(char *buf, size_t cap, wifi_form_result_t result,
                                 const char *ssid) {
    if (buf == NULL || cap == 0) return 0;
    writer_t w = { .buf = buf, .cap = cap, .len = 0, .overflow = false };
    buf[0] = '\0';

    begin_page(&w, "Not saved");
    w_str(&w, "<h2>Could not save</h2><p>");
    // WIFI_FORM_OK 不该走到这里；按通用错误文案兜底，不把空串显示给用户。
    w_str(&w, wifi_provision_form_error_text(result == WIFI_FORM_OK ? WIFI_FORM_ERR_NO_FIELD
                                                                   : result));
    w_str(&w, "</p>");
    if (ssid != NULL && ssid[0] != '\0') {
        w_str(&w, "<p>Network: ");
        w_esc(&w, ssid);
        w_str(&w, "</p>");
    }
    w_str(&w, "<p>Nothing was changed on the device.</p>");
    end_page(&w);
    return writer_finish(&w);
}

size_t wifi_provision_html_escape(char *buf, size_t cap, const char *text) {
    if (buf == NULL || cap == 0) return 0;
    writer_t w = { .buf = buf, .cap = cap, .len = 0, .overflow = false };
    buf[0] = '\0';
    w_esc(&w, text);
    return writer_finish(&w);
}

size_t wifi_provision_html_form(char *buf, size_t cap, const wifi_scan_list_t *list) {
    if (buf == NULL || cap == 0) return 0;
    buf[0] = '\0';

    const char *close = strstr(FORM_PAGE, WIFI_SCAN_DATALIST_END);
    if (close == NULL) return 0; // 标记被改坏了：宁可报错，也不要拼出半页
    const size_t head_len = (size_t)(close - FORM_PAGE);
    const size_t total = sizeof FORM_PAGE - 1;
    const size_t tail_len = total - head_len;

    if (list == NULL || list->count == 0) {
        // 没有列表时与静态页逐字节一致：降级路径不该有第二种样子。
        if (total + 1 > cap) return 0;
        memcpy(buf, FORM_PAGE, total + 1);
        return total;
    }

    writer_t w = { .buf = buf, .cap = cap, .len = 0, .overflow = false };
    w_raw(&w, FORM_PAGE, head_len);
    for (size_t i = 0; i < list->count; i++) {
        w_str(&w, "<option value=\"");
        w_esc(&w, list->items[i].ssid);
        w_str(&w, "\">");
        w_esc(&w, list->items[i].ssid);
        char dbm[16];
        snprintf(dbm, sizeof dbm, " (%d dBm)", (int)list->items[i].rssi);
        w_str(&w, dbm);
        w_str(&w, "</option>");
    }
    w_raw(&w, FORM_PAGE + head_len, tail_len);
    return writer_finish(&w);
}
