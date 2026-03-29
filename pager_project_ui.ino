#include <LittleFS.h> 
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include "ui.h"
#include "Preferences.h"

Preferences prefs;
String myID = "";
String myName = "";
String friendsData = ""; // We will store friends as a string like: "Shoib,1234567890;Arman,0987654321;"
String current_friend_name = ""; // Tracks whose chat is currently open

// --- SYSTEM STATE TRACKERS ---
enum Protocol { PROTOCOL_NONE, PROTOCOL_RADIO, PROTOCOL_INTERNET };
Protocol currentProtocol = PROTOCOL_NONE;


bool isWifiConnected = false;
bool isUsernameSet = false; // We will load this from Preferences later

/* --- 1. DISPLAY CONFIGURATION --- */
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * screenHeight / 10];

#define PIN_PREV  13   // Moves focus backward (e.g., Left Arrow)
#define PIN_NEXT  12   // Moves focus forward (e.g., Right Arrow)
#define PIN_ENTER 14  // Clicks the selected item (e.g., Enter/Select)


/* --- 2. MCP23017 KEYPAD CONFIGURATION --- */
Adafruit_MCP23X17 mcp;
const byte ROWS = 4;
const byte COLS = 8;
char keys[ROWS][COLS] = {
    {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'},
    {'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'},
    {'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X'},
    {'Y', 'Z', '\0', ' ', '\0', '\0', '\0', '\0'}
};
byte rowPins[ROWS] = {0, 1, 2, 3};      // MCP Port A
byte colPins[COLS] = {8, 9, 10, 11, 12, 13, 14, 15}; // MCP Port B

char lastKeyPressed = '\0';
unsigned long lastKeyTime = 0;
const int keypadDebounce = 150; 

/* --- 3. DEDICATED BUTTONS (Wired to ESP32) --- */
#define CAPS_PIN 26
bool capsLock = false; 
bool lastCapsBtnState = HIGH; 
bool capsBtnState = HIGH;
unsigned long lastCapsDebounce = 0;

#define BACKSPACE_PIN 27
bool lastBsBtnState = HIGH;
bool bsBtnState = HIGH;
unsigned long lastBsDebounce = 0;
const int btnDebounce = 50;

lv_group_t * ui_group; 
// Tracks exactly which screen we are looking at
lv_obj_t * current_active_screen = NULL;

// --- HC-12 SERIAL SETUP ---
#define HC12_RX_PIN 32  // Connect to HC-12 TX
#define HC12_TX_PIN 33  // Connect to HC-12 RX
HardwareSerial HC12(2); // Keep this on port 2!

// --- NAVIGATION EVENTS ---
void on_back_to_proto_clicked(lv_event_t * e) {
    change_screen(objects.select_protocol_win);
}

void on_back_to_friends_clicked(lv_event_t * e) {
    change_screen(objects.friends_list);
}

void on_back_from_wifi_clicked(lv_event_t * e) {
    change_screen(objects.select_protocol_win);
}


// --- NETWORK PROTOCOLS ---

void send_via_rf(String msg) {
    HC12.println(msg);
    Serial.println("Sent via Radio: " + msg);
}

void send_via_firebase(String msg) {
    // TODO: Implement Firebase ESP32 Client logic here
    Serial.println("Sent via Firebase (STUB): " + msg);
}

// checks the flash memory when the device turns on. If it's a brand-new device, it generates the 10-digit ID
void init_preferences() {
    prefs.begin("pager_data", false); 
    
    // Load existing data (defaults to "" if nothing is saved yet)
    myID = prefs.getString("my_id", "");
    myName = prefs.getString("my_name", "");
    friendsData = prefs.getString("friends", "");

    // STEP 3 LOGIC: Generate 10-digit ID if it doesn't exist
    if (myID == "") {
        for (int i = 0; i < 10; i++) {
            int r = random(0, 10);
            if (i == 0 && r == 0) r = 1; // Prevent starting with 0
            myID += String(r);
        }
        prefs.putString("my_id", myID);
        Serial.println("Generated new ID: " + myID);
    }

    isUsernameSet = (myName != "");
}

// --- EVENT: User highlighted a friend with the arrow keys ---
void on_friend_focused(lv_event_t * e) {
    lv_obj_t * focused_btn = lv_event_get_target(e);
    const char * friend_name = lv_list_get_btn_text(objects.friend_list_widget, focused_btn);
    
    if (friend_name != NULL) {
        String f_id = get_friend_id_by_name(String(friend_name));
        lv_label_set_text(objects.friend_info_lbl, ("ID: " + f_id).c_str());
    }
}

void change_screen(lv_obj_t * new_screen) {
    // ---> ADD THIS LINE: Explicitly remember where we are! <---
    current_active_screen = new_screen;

    // 1. Tell LVGL to draw the new screen
    lv_scr_load(new_screen);

    // 2. Clear the hardware button focus group
    lv_group_remove_all_objs(ui_group);

    // 3. Re-populate the focus group based on WHICH screen just opened
    if (new_screen == objects.select_protocol_win) {
        lv_group_add_obj(ui_group, objects.radio_btn);
        lv_group_add_obj(ui_group, objects.internet_btn);
        lv_group_focus_obj(objects.radio_btn); // Start on Radio by default
    } 
    else if (new_screen == objects.wifi_conf_win) {
        lv_group_add_obj(ui_group, objects.back_from_wifi_btn);
        lv_group_add_obj(ui_group, objects.ssid_textbox);
        lv_group_add_obj(ui_group, objects.pass_textbox);
        lv_group_add_obj(ui_group, objects.connect_btn);
        lv_group_focus_obj(objects.ssid_textbox);
    }
    else if (new_screen == objects.my_id_win) {
        lv_group_add_obj(ui_group, objects.usename); // Text input
        lv_group_add_obj(ui_group, objects.save_btn);
        lv_group_focus_obj(objects.usename);
    }
    else if (new_screen == objects.friends_list) {
        // Show My ID and Current Protocol ---
        String proto_text = (currentProtocol == PROTOCOL_RADIO) ? "[RADIO]" : "[WIFI]";
        String status_text = proto_text + " My ID: " + myID;
        lv_label_set_text(objects.my_status_lbl, status_text.c_str());

        // Add the top inputs, the Add button and the Back Button
        lv_group_add_obj(ui_group, objects.user_id);
        lv_group_add_obj(ui_group, objects.friend_name); 
        lv_group_add_obj(ui_group, objects.btn_add_friend);
        lv_group_add_obj(ui_group, objects.back_to_proto_btn);
        
        // --> FIX: Run the populator HERE, so it adds the friend buttons to the group AFTER it was cleared <--
        populate_friends_list();
        
        // Focus the name input box by default when entering the screen
        lv_group_focus_obj(objects.friend_name);
    }
    else if (new_screen == objects.chat_win) {
        lv_group_add_obj(ui_group, objects.input_box);
        lv_group_add_obj(ui_group, objects.send_btn);
        lv_group_add_obj(ui_group, objects.back_to_friends_btn);

        // --> LOAD THE HISTORY FROM FLASH MEMORY <--
        load_chat_history();

        // Loop through the freshly loaded wrappers and link the bubbles to the keys
        uint32_t child_cnt = lv_obj_get_child_cnt(objects.chat_container);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * historical_wrapper = lv_obj_get_child(objects.chat_container, i);
            
            // ---> FIX 2: NULL POINTER ARMOR <---
            // Make sure the wrapper actually has children before extracting child 0!
            if (historical_wrapper != NULL && lv_obj_get_child_cnt(historical_wrapper) > 0) {
                lv_obj_t * colored_bubble = lv_obj_get_child(historical_wrapper, 0);
                if (colored_bubble != NULL) {
                    lv_group_add_obj(ui_group, colored_bubble);
                }
            }
        }

        lv_group_focus_obj(objects.input_box);
    }
}

// --- Opening the Chat ---
void on_friend_selected(lv_event_t * e) {
    lv_obj_t * clicked_btn = lv_event_get_target(e);
    const char * friend_name = lv_list_get_btn_text(objects.friend_list_widget, clicked_btn);
    
    // --> FIX: Protect against Null Pointers! <--
    if (friend_name == NULL) {
        friend_name = "Friend"; 
    }
    
    current_friend_name = String(friend_name);
    lv_label_set_text(objects.user_name, friend_name);
    change_screen(objects.chat_win);
}


void populate_friends_list() {
    // Clear the list first so we don't get duplicates
    lv_obj_clean(objects.friend_list_widget);
    
    int startIdx = 0;
    int semiIdx = friendsData.indexOf(';', startIdx);
    
    while (semiIdx != -1) {
        String entry = friendsData.substring(startIdx, semiIdx);
        int commaIdx = entry.indexOf(',');
        
        if (commaIdx != -1) {
            String fName = entry.substring(0, commaIdx);
            
            // Drop the button into the LVGL list
            lv_obj_t * btn = lv_list_add_btn(objects.friend_list_widget, "", fName.c_str());
            lv_obj_add_event_cb(btn, on_friend_selected, LV_EVENT_CLICKED, NULL);
            lv_obj_add_event_cb(btn, on_friend_focused, LV_EVENT_FOCUSED, NULL);
            lv_group_add_obj(ui_group, btn); 
        }
        startIdx = semiIdx + 1;
        semiIdx = friendsData.indexOf(';', startIdx);
    }
}

void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)color_p, w * h, true); 
    tft.endWrite();
    lv_disp_flush_ready(disp_drv);
}

// --- PURE C++ BUBBLE GENERATOR (Flexbox & Scrollable) ---
lv_obj_t * create_raw_bubble(String message, bool is_sender) {
    // 1. The Wrapper
    lv_obj_t * wrapper = lv_obj_create(objects.chat_container);
    lv_obj_set_width(wrapper, lv_pct(100));
    lv_obj_set_height(wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);

    // ---> THE FIX: Turn wrapper into a Flex Row! <---
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);

    // 2. The Colored Bubble (Auto-sizing with Max Width)
    lv_obj_t * my_bubble = lv_obj_create(wrapper);
    lv_obj_set_size(my_bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(my_bubble, 240, LV_PART_MAIN); // Prevents text from flying off screen
    lv_obj_set_style_radius(my_bubble, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(my_bubble, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(my_bubble, 0, LV_PART_MAIN);
    lv_obj_clear_flag(my_bubble, LV_OBJ_FLAG_SCROLLABLE);
    
    // Tell LVGL this specific bubble is allowed to be focused and scrolled!
    lv_obj_add_flag(my_bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(my_bubble, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    // 3. The Text Label
    lv_obj_t * bubble_label = lv_label_create(my_bubble);
    lv_label_set_text(bubble_label, message.c_str());

    // 4. Safely push the bubble Left or Right using Flex Alignment
    if (is_sender) {
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_bg_color(my_bubble, lv_color_hex(0xDCF8C6), LV_PART_MAIN);
    } else {
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_bg_color(my_bubble, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    }

    return wrapper; 
}


/* --- 4. HARDWARE SCANNER FUNCTION --- */
char scanKeypad() {
  char pressedKey = '\0';
  for (int r = 0; r < ROWS; r++) {
    mcp.pinMode(rowPins[r], OUTPUT);
    mcp.digitalWrite(rowPins[r], LOW);
    for (int c = 0; c < COLS; c++) {
      if (mcp.digitalRead(colPins[c]) == LOW) {
        pressedKey = keys[r][c]; 
      }
    }
    mcp.pinMode(rowPins[r], INPUT); 
  }

  // Debounce the matrix
  if (pressedKey != '\0') {
    if (pressedKey != lastKeyPressed || (millis() - lastKeyTime) > keypadDebounce) {
      lastKeyPressed = pressedKey;
      lastKeyTime = millis();
      return pressedKey;
    }
  } else {
    lastKeyPressed = '\0'; 
  }
  return '\0'; 
}

void my_keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data) {
    static uint32_t last_key = 0;

    // --- 1. CHECK DEDICATED NAVIGATION PINS FIRST ---
    if (digitalRead(PIN_NEXT) == LOW) {
        data->state = LV_INDEV_STATE_PR;
        last_key = LV_KEY_NEXT; // Tell LVGL to move focus forward
        data->key = last_key;
        return; // Exit the function early!
    } 
    
    if (digitalRead(PIN_PREV) == LOW) {
        data->state = LV_INDEV_STATE_PR;
        last_key = LV_KEY_PREV; // Tell LVGL to move focus backward
        data->key = last_key;
        return; // Exit early!
    }
    
    if (digitalRead(PIN_ENTER) == LOW) {
        data->state = LV_INDEV_STATE_PR;
        last_key = LV_KEY_ENTER; // Tell LVGL to click the highlighted button
        data->key = last_key;
        return; // Exit early!
    }

    // --- 2. IF NO NAV BUTTONS PRESSED, CHECK THE TYPING MATRIX ---
    char key = scanKeypad(); // Your MCP23017 function
    
    if (key != '\0') {
        data->state = LV_INDEV_STATE_PR;

        // Apply your Caps Lock logic
        if (key >= 'A' && key <= 'Z' && !capsLock) {
            key = tolower(key); 
        }

        last_key = key;
        data->key = last_key;
    } else {
        // Nothing is pressed anywhere on the device
        data->state = LV_INDEV_STATE_REL;
    }
}



// --- EVENT: User clicked "Radio" ---
void on_radio_btn_clicked(lv_event_t * e) {
    currentProtocol = PROTOCOL_RADIO;
    
    if (!isUsernameSet) {
        // Show the ID on the screen before moving to it
        lv_label_set_text(objects.id_lbl, myID.c_str());
        change_screen(objects.my_id_win);
    } else {
        change_screen(objects.friends_list);
    }
}

// --- EVENT: User clicked "Internet" ---
void on_internet_btn_clicked(lv_event_t * e) {
    currentProtocol = PROTOCOL_INTERNET;
    
    if (!isWifiConnected) {
        change_screen(objects.wifi_conf_win); // Ask for WiFi details
    } else {
        change_screen(objects.friends_list); // Go straight to contacts
    }
}


// --- Saving Your Name ---
void on_save_btn_clicked(lv_event_t * e) {
    const char * typed_name = lv_textarea_get_text(objects.usename);
    
    if (strlen(typed_name) > 0) {
        myName = String(typed_name);
        prefs.putString("my_name", myName); // Save permanently
        isUsernameSet = true;
        change_screen(objects.friends_list);
    }
}

// --- Adding a Friend ---
void on_add_friend_clicked(lv_event_t * e) {
    const char * f_name = lv_textarea_get_text(objects.friend_name); 
    const char * f_id = lv_textarea_get_text(objects.user_id);
    
    if (strlen(f_name) > 0 && strlen(f_id) > 0) {
        // 1. Save to memory
        friendsData += String(f_name) + "," + String(f_id) + ";";
        prefs.putString("friends", friendsData);
        
        // 2. Add to the visible list
        lv_obj_t * btn = lv_list_add_btn(objects.friend_list_widget, "", f_name);
        lv_obj_add_event_cb(btn, on_friend_selected, LV_EVENT_CLICKED, NULL);
        
        // --> FIX: Add the new friend to the group <--
        lv_group_add_obj(ui_group, btn);
        
        // 3. Clear the text boxes for next time
        lv_textarea_set_text(objects.friend_name, "");
        lv_textarea_set_text(objects.user_id, "");
        
        // --> FIX: Move focus directly to the friend you just added! <--
        lv_group_focus_obj(btn);
    }
}


// --- Splash Screen Timer ---
void splash_timer_cb(lv_timer_t * timer) {
    change_screen(objects.select_protocol_win);
    lv_timer_del(timer); // Delete timer so it only runs once
}


// --- EVENT: User clicked the Send Button in the Chat ---
void on_send_btn_clicked(lv_event_t * e) {
    const char * raw_text = lv_textarea_get_text(objects.input_box);
    if (strlen(raw_text) == 0) return;

    // BUG FIX: Save to a permanent String BEFORE clearing the text box!
    String messageToSend = String(raw_text); 

    // 1. Memory Protection
    uint32_t current_msg_count = lv_obj_get_child_cnt(objects.chat_container);
    if (current_msg_count > 30) {
        lv_obj_t * oldest_wrapper = lv_obj_get_child(objects.chat_container, 0);
        // --> REMOVED lv_group_remove_obj <--
        lv_obj_del(oldest_wrapper); 
    }

    // 2. Spawn the wrapper & bubble (Set to TRUE for Sender)
    lv_obj_t * new_wrapper = create_raw_bubble(messageToSend, true);

    // Extract the colored bubble and link it to the hardware keys! <--
    lv_obj_t * colored_bubble = lv_obj_get_child(new_wrapper, 0);
    lv_group_add_obj(ui_group, colored_bubble);
    lv_obj_update_layout(objects.chat_container);
    lv_obj_scroll_to_view(new_wrapper, LV_ANIM_OFF);

    // 4. Clear input box and snap focus back
    lv_textarea_set_text(objects.input_box, "");
    lv_group_focus_obj(objects.input_box);

    // 5. DYNAMIC PROTOCOL ROUTING & PACKAGING
    String targetID = get_friend_id_by_name(current_friend_name);
    if (targetID == "") {
        Serial.println("Error: Could not find ID for " + current_friend_name);
        return; 
    }

    // Format: TARGET|SENDER|MESSAGE
    String packetToSend = targetID + "|" + myID + "|" + messageToSend;

    // Save the plain text to our local UI history
    save_chat_message(current_friend_name, currentProtocol, messageToSend, true);

    // Transmit the fully wrapped packet over the air
    if (currentProtocol == PROTOCOL_RADIO) {
        send_via_rf(packetToSend);
    } else if (currentProtocol == PROTOCOL_INTERNET) {
        send_via_firebase(packetToSend);
    }
}


// --- EVENT: Incoming Radio Message ---
void display_incoming_message(String incoming_text) {
    // STRICT RULE: Only touch the UI if the screen is actively open!
    if (current_active_screen == objects.chat_win) {
        
        // 1. Memory Protection (Limit to 30 messages)
        uint32_t current_msg_count = lv_obj_get_child_cnt(objects.chat_container);
        if (current_msg_count > 30) {
            lv_obj_t * oldest_wrapper = lv_obj_get_child(objects.chat_container, 0);
            lv_obj_del(oldest_wrapper); 
        }

        // 2. Spawn the wrapper & bubble (Set to FALSE for Receiver)
        // Notice we just pass incoming_text directly now!
        lv_obj_t * new_wrapper = create_raw_bubble(incoming_text, false);

        // 3. --> FIX 2: Link the colored bubble to the hardware keys <--
        lv_obj_t * colored_bubble = lv_obj_get_child(new_wrapper, 0);
        lv_group_add_obj(ui_group, colored_bubble);
        lv_obj_update_layout(objects.chat_container);
        lv_obj_scroll_to_view(colored_bubble, LV_ANIM_OFF);
    }
}

// --- FILE SYSTEM: SAVE MESSAGE ---
void save_chat_message(String friend_name, Protocol proto, String msg, bool is_sender) {
    if (friend_name == "") return;

    // Create a unique file name: e.g., "/Arman_RADIO.txt"
    String proto_str = (proto == PROTOCOL_RADIO) ? "RADIO" : "INTERNET";
    String filename = "/" + friend_name + "_" + proto_str + ".txt";

    // Open file in APPEND mode so we don't erase history
    File file = LittleFS.open(filename, FILE_APPEND);
    if (!file) {
        Serial.println("Error: Failed to open SPIFFS file for appending.");
        return;
    }

    // Write the message with a prefix to know what color bubble to make later
    String prefix = is_sender ? "S|" : "R|";
    file.print(prefix + msg + "\n");
    file.close();
    Serial.println("Saved to Flash: " + filename);
}

// --- FILE SYSTEM: LOAD HISTORY ---
void load_chat_history() {
    lv_obj_clean(objects.chat_container);

    if (current_friend_name == "") return;

    String proto_str = (currentProtocol == PROTOCOL_RADIO) ? "RADIO" : "INTERNET";
    String filename = "/" + current_friend_name + "_" + proto_str + ".txt";

    File file = LittleFS.open(filename, FILE_READ);
    if (!file) {
        return; 
    }

    int messages_loaded = 0; // Track how many we process

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 2) {
            bool is_sender = (line.charAt(0) == 'S');
            String actual_msg = line.substring(2); 
            create_raw_bubble(actual_msg, is_sender);

            // ---> FIX 1: RAM PROTECTION DURING LOAD <---
            // Instantly delete old bubbles so we never exceed 30 on screen
            if (lv_obj_get_child_cnt(objects.chat_container) > 30) {
                lv_obj_t * oldest_wrapper = lv_obj_get_child(objects.chat_container, 0);
                lv_obj_del(oldest_wrapper);
            }

            // ---> FIX 2: WATCHDOG TIMER PROTECTION <---
            messages_loaded++;
            if (messages_loaded % 5 == 0) {
                delay(2); // Let the ESP32 "breathe" every 5 lines so it doesn't freeze!
            }
        }
    }
    file.close();

    // ---> THE SAFE SCROLL <---
    lv_obj_update_layout(objects.chat_container);
    uint32_t child_cnt = lv_obj_get_child_cnt(objects.chat_container);
    if (child_cnt > 0) {
        lv_obj_t * last_wrapper = lv_obj_get_child(objects.chat_container, child_cnt - 1);
        
        // Ensure the wrapper actually contains a bubble before scrolling to it
        if (last_wrapper != NULL && lv_obj_get_child_cnt(last_wrapper) > 0) {
            lv_obj_t * colored_bubble = lv_obj_get_child(last_wrapper, 0);
            lv_obj_scroll_to_view(colored_bubble, LV_ANIM_OFF);
        }
    }
}

// --- PHONEBOOK HELPERS ---
String get_friend_id_by_name(String targetName) {
    int startIdx = 0;
    int semiIdx = friendsData.indexOf(';', startIdx);
    while (semiIdx != -1) {
        String entry = friendsData.substring(startIdx, semiIdx);
        int commaIdx = entry.indexOf(',');
        if (commaIdx != -1) {
            if (entry.substring(0, commaIdx) == targetName) return entry.substring(commaIdx + 1);
        }
        startIdx = semiIdx + 1;
        semiIdx = friendsData.indexOf(';', startIdx);
    }
    return "";
}

String get_friend_name_by_id(String targetID) {
    int startIdx = 0;
    int semiIdx = friendsData.indexOf(';', startIdx);
    while (semiIdx != -1) {
        String entry = friendsData.substring(startIdx, semiIdx);
        int commaIdx = entry.indexOf(',');
        if (commaIdx != -1) {
            if (entry.substring(commaIdx + 1) == targetID) return entry.substring(0, commaIdx);
        }
        startIdx = semiIdx + 1;
        semiIdx = friendsData.indexOf(';', startIdx);
    }
    return "";
}

// --- INCOMING PACKET ROUTER ---
void process_incoming_packet(String packet) {
    // 1. Check if it's actually our structured packet format
    int firstPipe = packet.indexOf('|');
    int secondPipe = packet.indexOf('|', firstPipe + 1);
    if (firstPipe == -1 || secondPipe == -1) return; // Ignore stray radio static

    // 2. Break the packet apart
    String targetID = packet.substring(0, firstPipe);
    String senderID = packet.substring(firstPipe + 1, secondPipe);
    String actualMessage = packet.substring(secondPipe + 1);

    // 3. SECURITY GATE 1: Is this message for me?
    if (targetID != myID) {
        Serial.println("Dropped: Message intended for ID " + targetID);
        return; 
    }

    // 4. SECURITY GATE 2: Do I know this sender? (Mutual Friends Only)
    String senderName = get_friend_name_by_id(senderID);
    if (senderName == "") {
        Serial.println("Dropped: Unauthorized message from unknown ID: " + senderID);
        return; // Instantly kill the process. Do not save, do not show.
    }

    // 5. SAVE IT TO THEIR BACKGROUND FILE
    save_chat_message(senderName, PROTOCOL_RADIO, actualMessage, false);

    // 6. VISUALS: Only draw the bubble if we are actively chatting with them right now!
    if (current_active_screen == objects.chat_win && current_friend_name == senderName) {
        display_incoming_message(actualMessage);
    } else {
        Serial.println("Background message saved for: " + senderName);
    }
}

/* --- 5. SETUP --- */
void setup() {
    Serial.begin(115200);

    // --- INITIALIZE THE FLASH HARD DRIVE ---
    if (!LittleFS.begin(true)) {
        Serial.println("CRITICAL ERROR: LittleFS Mount Failed!");
    } else {
        Serial.println("LittleFS File System Mounted Successfully.");
    }

    HC12.begin(9600, SERIAL_8N1, HC12_RX_PIN, HC12_TX_PIN);
    HC12.setTimeout(20);
    Serial.println("HC-12 Radio Initialized.");

    // Setup Dedicated Buttons
    pinMode(CAPS_PIN, INPUT_PULLUP);
    pinMode(BACKSPACE_PIN, INPUT_PULLUP);
    pinMode(PIN_PREV, INPUT_PULLUP);
    pinMode(PIN_NEXT, INPUT_PULLUP);
    pinMode(PIN_ENTER, INPUT_PULLUP);

    // Setup I2C and MCP23017
    Wire.begin(); 
    if (!mcp.begin_I2C(0x20)) {
        Serial.println("MCP23017 not found!");
        while (1);
    }
    for (int c = 0; c < COLS; c++) mcp.pinMode(colPins[c], INPUT_PULLUP);
    for (int r = 0; r < ROWS; r++) mcp.pinMode(rowPins[r], INPUT);

    // Setup TFT & LVGL
    tft.init();
    tft.setRotation(3);
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 10);
    
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    
    // 1. Register our new "Mixer" function as the official LVGL input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = my_keypad_read;
    lv_indev_t * my_system_input = lv_indev_drv_register(&indev_drv);

    // Load EEZ Studio UI
    ui_init();

    // Boot up the memory and check who we are
    init_preferences();

    // Attach our Event Callbacks to the EEZ Studio widgets
    lv_obj_add_event_cb(objects.radio_btn, on_radio_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.internet_btn, on_internet_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.save_btn, on_save_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.btn_add_friend, on_add_friend_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.send_btn, on_send_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.back_to_proto_btn, on_back_to_proto_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.back_to_friends_btn, on_back_to_friends_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.back_from_wifi_btn, on_back_from_wifi_clicked, LV_EVENT_CLICKED, NULL);
    
    // Create the UI Group and link it to the keypad
    ui_group = lv_group_create(); 
    lv_indev_set_group(my_system_input, ui_group);

    // Start the UI Flow using your Screen Router!
    change_screen(objects.splash_win);
    
    // Set a timer to automatically move past the Splash screen after 3 seconds
    lv_timer_create(splash_timer_cb, 3000, NULL);
    
    Serial.println("System Ready!");
}

/* --- 6. MAIN LOOP --- */
void loop() {
    // 1. Keep the UI rendering
    lv_tick_inc(5);
    lv_timer_handler();
    delay(5);

    // 2. --- CHECK RADIO FIRST (Non-Blocking) ---
    if (HC12.available()) {
        String incomingStr = HC12.readStringUntil('\n');
        incomingStr.trim(); 
        if (incomingStr.length() > 0) {
            // --> Send the raw text to our new Packet Router! <--
            process_incoming_packet(incomingStr);
        }
    }
    // Ensure the UI is fully loaded before trying to type into it
    if (objects.input_box == NULL) return;

    // 2. CHECK CAPS LOCK BUTTON
    bool capsReading = digitalRead(CAPS_PIN);
    if (capsReading != lastCapsBtnState) lastCapsDebounce = millis(); 
    if ((millis() - lastCapsDebounce) > btnDebounce) {
        if (capsReading != capsBtnState) {
            capsBtnState = capsReading;
            if (capsBtnState == LOW) {
                capsLock = !capsLock; 
                Serial.println(capsLock ? "Caps: ON" : "Caps: OFF");
            }
        }
    }
    lastCapsBtnState = capsReading; 

    // 3. CHECK BACKSPACE BUTTON
    bool bsReading = digitalRead(BACKSPACE_PIN);
    if (bsReading != lastBsBtnState) lastBsDebounce = millis(); 
    if ((millis() - lastBsDebounce) > btnDebounce) {
        if (bsReading != bsBtnState) {
            bsBtnState = bsReading;
            if (bsBtnState == LOW) {
                // Send backspace to whatever widget currently has focus!
                lv_group_send_data(ui_group, LV_KEY_BACKSPACE);
                Serial.println("Backspace pressed");
            }
        }
    }
    lastBsBtnState = bsReading;
}
