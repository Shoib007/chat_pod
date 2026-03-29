#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_CHAT_WIN = 1,
    SCREEN_ID_SPLASH_WIN = 2,
    SCREEN_ID_SELECT_PROTOCOL_WIN = 3,
    SCREEN_ID_MY_ID_WIN = 4,
    SCREEN_ID_WIFI_CONF_WIN = 5,
    SCREEN_ID_FRIENDS_LIST = 6,
    _SCREEN_ID_LAST = 6
};

typedef struct _objects_t {
    lv_obj_t *chat_win;
    lv_obj_t *splash_win;
    lv_obj_t *select_protocol_win;
    lv_obj_t *my_id_win;
    lv_obj_t *wifi_conf_win;
    lv_obj_t *friends_list;
    lv_obj_t *obj0;
    lv_obj_t *back_to_friends_btn;
    lv_obj_t *chat_container;
    lv_obj_t *user_name;
    lv_obj_t *input_box;
    lv_obj_t *send_btn;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *radio_btn;
    lv_obj_t *internet_btn;
    lv_obj_t *obj3;
    lv_obj_t *id_lbl;
    lv_obj_t *usename;
    lv_obj_t *save_btn;
    lv_obj_t *ssid_textbox;
    lv_obj_t *pass_textbox;
    lv_obj_t *connect_btn;
    lv_obj_t *status_led;
    lv_obj_t *back_from_wifi_btn;
    lv_obj_t *friend_list_widget;
    lv_obj_t *btn_add_friend;
    lv_obj_t *user_id;
    lv_obj_t *friend_name;
    lv_obj_t *my_status_lbl;
    lv_obj_t *friend_info_lbl;
    lv_obj_t *back_to_proto_btn;
} objects_t;

extern objects_t objects;

void create_screen_chat_win();
void tick_screen_chat_win();

void create_screen_splash_win();
void tick_screen_splash_win();

void create_screen_select_protocol_win();
void tick_screen_select_protocol_win();

void create_screen_my_id_win();
void tick_screen_my_id_win();

void create_screen_wifi_conf_win();
void tick_screen_wifi_conf_win();

void create_screen_friends_list();
void tick_screen_friends_list();

void create_user_widget_chat_bubble(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_chat_bubble(int startWidgetIndex);

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/