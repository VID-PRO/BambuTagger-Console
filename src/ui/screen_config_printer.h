#pragma once
/**
 * screen_config_printer.h  —  Printer Configuration Screen
 *
 * Supports up to 4 printer configurations (IP, serial, access code).
 * Values are persisted in NVS with indexed keys (bam_ip_0, etc.).
 */
#include <lvgl.h>
#include <functional>
#include "../config.h"

typedef std::function<void()> SaveConnectCallback;

class ScreenConfigPrinter {
public:
    void create(lv_obj_t *parent);

    void onSaveConnect(SaveConnectCallback cb) { _save_cb = cb; }

    /** Load all printer configs from the array. */
    void loadValues(const PrinterConfig configs[], int count);

    lv_obj_t *root() const { return _root; }

private:
    static void _kb_event_cb(lv_event_t *e);
    static void _ta_event_cb(lv_event_t *e);
    static void _save_event_cb(lv_event_t *e);
    static void _count_btn_cb(lv_event_t *e);

    struct PrinterFields {
        lv_obj_t *ta_ip     = nullptr;
        lv_obj_t *ta_serial = nullptr;
        lv_obj_t *ta_code   = nullptr;
        lv_obj_t *cont      = nullptr;
    } _fields[MAX_PRINTERS];

    lv_obj_t           *_root        = nullptr;
    lv_obj_t           *_scroll      = nullptr;
    lv_obj_t           *_kb          = nullptr;
    lv_obj_t           *_lbl_status  = nullptr;
    lv_obj_t           *_btn_minus   = nullptr;
    lv_obj_t           *_btn_plus    = nullptr;
    lv_obj_t           *_lbl_count   = nullptr;
    int                 _num_printers = 1;
    SaveConnectCallback _save_cb;

    void _rebuildFields();
    lv_obj_t *_makeField(lv_obj_t *parent, const char *label_text, bool password = false);
};
