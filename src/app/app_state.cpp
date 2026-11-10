#include "app/app_state.h"

char    cfg_wifi_ssid[33]     = "";
char    cfg_wifi_password[65] = "";
bool    cfg_lang_set          = false;
bool    cfg_first_boot        = true;
bool    cfg_setup_resume      = false;
uint8_t cfg_failed_boots      = 0;

bool setup_active = false;
bool wifi_ok      = false;

int bright_normal    = BRIGHT_NORMAL_DEFAULT;
int dim_timeout_ms   = DIM_TIMEOUT_DEFAULT;
int off_timeout_ms   = OFF_TIMEOUT_DEFAULT;
int sleep_timeout_ms = SLEEP_TIMEOUT_DEFAULT;

#ifdef APP_UI_LVGL
lv_obj_t *scr_main            = nullptr;
lv_obj_t *scr_settings        = nullptr;
lv_obj_t *scr_wifi            = nullptr;
lv_obj_t *scr_timezone        = nullptr;
lv_obj_t *scr_welcome         = nullptr;
lv_obj_t *scr_first_boot      = nullptr;
lv_obj_t *scr_setup_done      = nullptr;
lv_obj_t *scr_wifi_setup      = nullptr;
lv_obj_t *scr_wifi_menu       = nullptr;
lv_obj_t *scr_wifi_pass       = nullptr;
lv_obj_t *scr_wifi_connecting = nullptr;
lv_obj_t *scr_wifi_portal     = nullptr;
lv_obj_t *scr_display         = nullptr;
lv_obj_t *scr_system          = nullptr;
lv_obj_t *scr_ota             = nullptr;
lv_obj_t *scr_ota_browser     = nullptr;
lv_obj_t *scr_ota_github      = nullptr;

lv_obj_t *lbl_ota_status   = nullptr;
lv_obj_t *lbl_burger_badge = nullptr;
lv_obj_t *lbl_system_badge = nullptr;
lv_obj_t *lbl_fw_badge     = nullptr;
lv_obj_t *lbl_gh_btn_badge = nullptr;
lv_obj_t *lbl_wifi_info    = nullptr;
#endif
