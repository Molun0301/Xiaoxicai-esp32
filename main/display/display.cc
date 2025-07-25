#include <esp_log.h>
#include <esp_err.h>
#include <string>
#include <cstdlib>
#include <cstring>

#include "display.h"
#include "board.h"
#include "application.h"
#include "font_awesome_symbols.h"
#include "audio_codec.h"
#include "settings.h"
#include "assets/lang_config.h"

#include "lvgl.h" 

#define TAG "Display"

Display::Display() {
    // Load theme from settings
    Settings settings("display", false);
    current_theme_name_ = settings.GetString("theme", "light");

    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
        .callback = [](void *arg) {
            Display *display = static_cast<Display*>(arg);
            DisplayLockGuard lock(display);
            lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Update display timer
    esp_timer_create_args_t update_display_timer_args = {
        .callback = [](void *arg) {
            Display *display = static_cast<Display*>(arg);
            display->Update();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_update_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&update_display_timer_args, &update_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(update_timer_, 1000000));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

Display::~Display() {
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }
    if (update_timer_ != nullptr) {
        esp_timer_stop(update_timer_);
        esp_timer_delete(update_timer_);
    }

    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
        lv_obj_del(notification_label_);
        lv_obj_del(status_label_);
        lv_obj_del(mute_label_);
        lv_obj_del(battery_label_);
        lv_obj_del(emotion_label_);
    }
    if( low_battery_popup_ != nullptr ) {
        lv_obj_del(low_battery_popup_);
    }
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

void Display::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
}

void Display::ShowNotification(const std::string &notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        return;
    }
    lv_label_set_text(notification_label_, notification);
    lv_obj_clear_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

void Display::Update() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) {
            return;
        }

        // 如果静音状态改变，则更新图标
        if (codec->output_volume() == 0 && !muted_) {
            muted_ = true;
            lv_label_set_text(mute_label_, FONT_AWESOME_VOLUME_MUTE);
        } else if (codec->output_volume() > 0 && muted_) {
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }
}

void Display::UpdateStatusBar(bool update_all) {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) {
            return;
        }

        // 如果静音状态改变，则更新图标
        if (codec->output_volume() == 0 && !muted_) {
            muted_ = true;
            lv_label_set_text(mute_label_, FONT_AWESOME_VOLUME_MUTE);
        } else if (codec->output_volume() > 0 && muted_) {
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }

    esp_pm_lock_acquire(pm_lock_);
    // 更新电池图标
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        if (charging) {
            icon = FONT_AWESOME_BATTERY_CHARGING;
        } else {
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY, // 0-19%
                FONT_AWESOME_BATTERY_1,    // 20-39%
                FONT_AWESOME_BATTERY_2,    // 40-59%
                FONT_AWESOME_BATTERY_3,    // 60-79%
                FONT_AWESOME_BATTERY_FULL, // 80-99%
                FONT_AWESOME_BATTERY_FULL, // 100%
            };
            icon = levels[battery_level / 20];
        }
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && battery_icon_ != icon) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, battery_icon_);
        }

        if (low_battery_popup_ != nullptr) {
            if (strcmp(icon, FONT_AWESOME_BATTERY_EMPTY) == 0 && discharging) {
                if (lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) { // 如果低电量提示框隐藏，则显示
                    lv_obj_clear_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                    auto& app = Application::GetInstance();
                    app.PlaySound(Lang::Sounds::P3_LOW_BATTERY);
                }
            } else {
                // Hide the low battery popup when the battery is not empty
                if (!lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) { // 如果低电量提示框显示，则隐藏
                    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // 升级固件时，不读取 4G 网络状态，避免占用 UART 资源
    auto device_state = Application::GetInstance().GetDeviceState();
    static const std::vector<DeviceState> allowed_states = {
        kDeviceStateIdle,
        kDeviceStateStarting,
        kDeviceStateWifiConfiguring,
        kDeviceStateListening,
        kDeviceStateActivating,
    };
    if (std::find(allowed_states.begin(), allowed_states.end(), device_state) != allowed_states.end()) {
        icon = board.GetNetworkStateIcon();
        if (network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
            DisplayLockGuard lock(this);
            network_icon_ = icon;
            lv_label_set_text(network_label_, network_icon_);
        }
    }

    esp_pm_lock_release(pm_lock_);
}


void Display::SetEmotion(const char* emotion) {
    struct Emotion {
        const char* icon;
        const char* text;
    };

    static const std::vector<Emotion> emotions = {
        {FONT_AWESOME_EMOJI_NEUTRAL, "neutral"},
        {FONT_AWESOME_EMOJI_HAPPY, "happy"},
        {FONT_AWESOME_EMOJI_LAUGHING, "laughing"},
        {FONT_AWESOME_EMOJI_FUNNY, "funny"},
        {FONT_AWESOME_EMOJI_SAD, "sad"},
        {FONT_AWESOME_EMOJI_ANGRY, "angry"},
        {FONT_AWESOME_EMOJI_CRYING, "crying"},
        {FONT_AWESOME_EMOJI_LOVING, "loving"},
        {FONT_AWESOME_EMOJI_EMBARRASSED, "embarrassed"},
        {FONT_AWESOME_EMOJI_SURPRISED, "surprised"},
        {FONT_AWESOME_EMOJI_SHOCKED, "shocked"},
        {FONT_AWESOME_EMOJI_THINKING, "thinking"},
        {FONT_AWESOME_EMOJI_WINKING, "winking"},
        {FONT_AWESOME_EMOJI_COOL, "cool"},
        {FONT_AWESOME_EMOJI_RELAXED, "relaxed"},
        {FONT_AWESOME_EMOJI_DELICIOUS, "delicious"},
        {FONT_AWESOME_EMOJI_KISSY, "kissy"},
        {FONT_AWESOME_EMOJI_CONFIDENT, "confident"},
        {FONT_AWESOME_EMOJI_SLEEPY, "sleepy"},
        {FONT_AWESOME_EMOJI_SILLY, "silly"},
        {FONT_AWESOME_EMOJI_CONFUSED, "confused"}
    };
    
    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });
    
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }

    // 如果找到匹配的表情就显示对应图标，否则显示默认的neutral表情
    if (it != emotions.end()) {
        lv_label_set_text(emotion_label_, it->icon);
    } else {
        lv_label_set_text(emotion_label_, FONT_AWESOME_EMOJI_NEUTRAL);
    }
}

void Display::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_label_set_text(emotion_label_, icon);
}

void Display::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }
    lv_label_set_text(chat_message_label_, content);
}

void Display::SetTheme(const std::string& theme_name) {
    current_theme_name_ = theme_name;
    Settings settings("display", true);
    settings.SetString("theme", theme_name);
}


void Display::CreateCanvas() {
    DisplayLockGuard lock(this);
    
    // 如果已经有画布，先销毁
    if (canvas_ != nullptr) {
        DestroyCanvas();
    }
    
    // 创建画布所需的缓冲区
    // 每个像素2字节(RGB565)
    size_t buf_size = height_ * width_  * 2;  // RGB565: 2 bytes per pixel
    
    // 分配内存，优先使用PSRAM
    canvas_buffer_ = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (canvas_buffer_ == nullptr) {
        ESP_LOGE("Display", "Failed to allocate canvas buffer");
        return;
    }
    
    // 获取活动屏幕
    lv_obj_t* screen = lv_screen_active();
    
    // 创建画布对象
    canvas_ = lv_canvas_create(screen);
    if (canvas_ == nullptr) {
        ESP_LOGE("Display", "Failed to create canvas");
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
        return;
    }
    
    // 初始化画布
    lv_canvas_set_buffer(canvas_, canvas_buffer_, height_, width_, LV_COLOR_FORMAT_RGB565);
    
    // 设置画布位置为全屏
    lv_obj_set_pos(canvas_, 0, 25);
    lv_obj_set_size(canvas_, 320 , 240);
    lv_obj_set_scrollbar_mode(canvas_, LV_SCROLLBAR_MODE_OFF);

    // 设置画布为透明
    lv_canvas_fill_bg(canvas_, lv_color_make(0, 0, 0), LV_OPA_TRANSP);

    ESP_LOGI("Display", "Canvas created successfully");
}

void Display::DestroyCanvas() {
    DisplayLockGuard lock(this);
    
    if (canvas_ != nullptr) {
        lv_obj_del(canvas_);
        canvas_ = nullptr;
    }
    
    if (canvas_buffer_ != nullptr) {
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
    }
    
    ESP_LOGI("Display", "Canvas destroyed");
}

void Display::DrawImageOnCanvas(int x, int y, int width, int height, const uint8_t* img_data) {
    DisplayLockGuard lock(this);
    
    // 确保有画布
    if (canvas_ == nullptr) {
        ESP_LOGE("Display", "Canvas not created");
        return;
    }
    
    
    // 创建一个描述器来映射图像数据
    const lv_image_dsc_t img_dsc = {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = (uint32_t)width,
            .h = (uint32_t)height,
            .stride = (uint32_t)(width * 2),  // RGB565: 2 bytes per pixel
            .reserved_2 = 0,
        },
        .data_size = (uint32_t)(width * height * 2),  // RGB565: 2 bytes per pixel
        .data = img_data,
        .reserved = NULL
    };
    
    // 使用图层绘制图像到画布上
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);
    
    lv_draw_image_dsc_t draw_dsc;
    lv_draw_image_dsc_init(&draw_dsc);
    draw_dsc.src = &img_dsc;
    
    lv_area_t area;
    area.x1 = 0;
    area.y1 = 0;
    area.x2 = 320;
    area.y2 = 240;
    
    lv_draw_image(&layer, &draw_dsc, &area);
    lv_canvas_finish_layer(canvas_, &layer);
    lv_obj_set_scrollbar_mode(canvas_, LV_SCROLLBAR_MODE_OFF);

    // 确保聊天内容在最上层
    lv_obj_move_foreground(chat_message_label_);
    
    // ESP_LOGI("Display", "Image drawn on canvas at x=%d, y=%d, w=%d, h=%d", x, y, width, height);
}
