#include "wifi_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "esp32_camera.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <string>

// #include"images/hcz/output_0001.h"
// #include"images/hcz/output_0002.h"
// #include"images/hcz/output_0003.h"
// #include"images/hcz/output_0004.h"
// #include"images/hcz/output_0005.h"
// #include"images/hcz/output_0006.h"
// #include"images/hcz/output_0007.h"
// #include"images/hcz/output_0008.h"
// #include"images/hcz/output_0009.h"
// #include"images/hcz/output_0010.h"
// #include"images/hcz/output_0011.h"
// #include"images/hcz/output_0012.h"
// #include"images/hcz/output_0013.h"
// #include"images/hcz/output_0014.h"
// #include"images/hcz/output_0015.h"
// #include"images/hcz/output_0016.h"

#include"images\cute\neutral.c"
#include"images\cute\laughing.c"
#include"images\cute\angry.c"
#include"images\cute\confused.c"
#include"images\cute\crying.c"
#include"images\cute\embarrassed.c"
#include"images\cute\funny.c"
#include"images\cute\relaxed.c"
#include"images\cute\silly.c"

#include "esp_lcd_ili9341.h"

 
#define TAG "CompactWifiBoardS3Cam"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

class CompactWifiBoardS3Cam : public WifiBoard {
private:
 
    Button boot_button_;
    LcdDisplay* display_;
     Esp32Camera* camera_;
    TaskHandle_t image_task_handle_ = nullptr; // 图片显示任务句柄
    
    // 情绪系统变量
    std::string current_emotion_ = "neutral";     // 当前情绪（统一变量）
    bool is_in_dialog_ = false;                  // 是否在对话中
    TickType_t last_action_time_ = 0;            // 上次动作执行时间

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }
    // 启动图片循环显示任务
    void StartImageSlideshow() {
        xTaskCreate(ImageSlideshowTask, "img_slideshow", 4096, this, 3, &image_task_handle_);
        ESP_LOGI(TAG, "图片循环显示任务已启动");
    }
    
    // 图片循环显示任务函数
    static void ImageSlideshowTask(void* arg) {
        CompactWifiBoardS3Cam* board = static_cast<CompactWifiBoardS3Cam*>(arg);
        Display* display = board->GetDisplay();
        
        if (!display) {
            ESP_LOGE(TAG, "无法获取显示设备");
            vTaskDelete(NULL);
            return;
        }
        
        // 获取应用实例
        auto& app = Application::GetInstance();
        
        // 创建画布（如果不存在）
        if (!display->HasCanvas()) {
            display->CreateCanvas();
        }
        
        // 设置图片显示参数
        int imgWidth = 320;
        int imgHeight = 240;
        int x = 0;
        int y = 0;
        
        // 创建临时缓冲区用于字节序转换
        uint16_t* convertedData = new uint16_t[imgWidth * imgHeight];
        if (!convertedData) {
            ESP_LOGE(TAG, "无法分配内存进行图像转换");
            vTaskDelete(NULL);
            return;
        }
        
        // 图片显示辅助函数
        auto displayImage = [&](const uint8_t* image) {
            for (int i = 0; i < imgWidth * imgHeight; i++) {
                uint16_t pixel = ((uint16_t*)image)[i];
                convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
            }
            display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
        };
        
        // 根据情绪获取对应图片的函数
        auto getImageByEmotion = [&](const std::string& emotion) -> const uint8_t* {
            if (emotion == "laughing") {
                return gImage_laughing;
            } else if (emotion == "funny") {
                return gImage_funny;
            } else if (emotion == "silly") {
                return gImage_silly;
            } else if (emotion == "angry") {
                return gImage_angry;
            } else if (emotion == "crying") {
                return gImage_crying;
            } else if (emotion == "embarrassed") {
                return gImage_embarrassed;
            } else if (emotion == "confused") {
                return gImage_confused;
            } else if (emotion == "relaxed") {
                return gImage_relaxed;
            } else if (emotion == "neutral") {
                return gImage_neutral;
            } else if (emotion == "loving" || emotion == "kissy" || emotion == "winking" || 
                       emotion == "cool" || emotion == "confident") {
                return gImage_funny;  // 这些情绪使用原来的funny图片
            } else if (emotion == "sleepy" || emotion == "happy") {
                return gImage_relaxed;  // 这些情绪使用relaxed图片
            } else if (emotion == "sad" || emotion == "shocked" || 
                       emotion == "surprised" || emotion == "thinking" || emotion == "delicious") {
                return gImage_neutral;  // 这些情绪使用normal图片
            } else {
                return gImage_neutral;  // 默认使用neutral
            }
        };
        
        // 初始化随机数种子
        srand(esp_timer_get_time() / 1000);
        
        // 自然情绪状态
        enum NaturalEmotionState {
            NATURAL_NORMAL,        // 正常睁眼
            NATURAL_CLOSE,         // 眨眼闭眼
            NATURAL_RANDOM_EMOTION // 随机情绪
        };
        
        NaturalEmotionState naturalState = NATURAL_NORMAL;
        TickType_t naturalStateChangeTime = xTaskGetTickCount();
        TickType_t naturalNextChangeTime = 0;
        TickType_t lastRandomEmotionCheck = 0;  // 上次检查随机情绪概率的时间
        bool canCheckRandomEmotion = true;      // 是否可以检查随机情绪概率
        bool hasCompletedBlinkCycle = false;    // 是否已完成一轮眨眼循环
        bool wasSpeaking = false;               // 上次是否在说话
        bool wasListening = false;              // 上次是否在听话
        
        // 默认显示gImage_neutral
        displayImage(gImage_neutral);
        ESP_LOGI(TAG, "初始显示normal图片");
        
        // 自然情绪模块处理函数
        auto processNaturalEmotion = [&](TickType_t currentTime) {
            // 检查随机情绪触发（每秒检查5%概率）
            if (canCheckRandomEmotion && naturalState != NATURAL_RANDOM_EMOTION && 
                (currentTime - lastRandomEmotionCheck) >= pdMS_TO_TICKS(1000)) {
                
                lastRandomEmotionCheck = currentTime;
                
                if ((rand() % 100) < 5) { // 5%概率
                    // 随机选择一个情绪
                    const std::string randomEmotions[] = {"laughing", "funny", "silly", "relaxed", "confused", "angry", "crying", "embarrassed"};
                    int emotionIndex = rand() % 8;
                    std::string randomEmotion = randomEmotions[emotionIndex];
                    board->SetCurrentEmotion(randomEmotion);
                    
                    naturalState = NATURAL_RANDOM_EMOTION;
                    naturalStateChangeTime = currentTime;
                    naturalNextChangeTime = currentTime + pdMS_TO_TICKS(3000); // 显示3秒
                    canCheckRandomEmotion = false;
                    hasCompletedBlinkCycle = false;
                    
                    const uint8_t* emotionImage = getImageByEmotion(randomEmotion);
                    displayImage(emotionImage);
                    
                    // 执行随机情绪动作
                    board->ExecuteEmotionAction(randomEmotion);
                    
                    ESP_LOGI(TAG, "自然情绪：触发随机情绪 %s", randomEmotion.c_str());
                    return;
                }
            }
            
            // 眨眼循环状态机
            if (currentTime >= naturalNextChangeTime) {
                switch (naturalState) {
                    case NATURAL_NORMAL:
                        // 切换到眨眼状态
                        naturalState = NATURAL_CLOSE;
                        naturalStateChangeTime = currentTime;
                        naturalNextChangeTime = currentTime + pdMS_TO_TICKS(500); // 眨眼0.5秒
                        
                        displayImage(gImage_relaxed);
                        ESP_LOGI(TAG, "自然情绪：眨眼");
                        break;
                        
                    case NATURAL_CLOSE:
                        // 眨眼结束，切换到正常状态
                        naturalState = NATURAL_NORMAL;
                        naturalStateChangeTime = currentTime;
                        naturalNextChangeTime = currentTime + pdMS_TO_TICKS(3000 + (rand() % 2000)); // 3-5秒随机
                        hasCompletedBlinkCycle = true;
                        lastRandomEmotionCheck = currentTime; // 重置随机情绪检查时间
                        
                        // 完成一轮眨眼循环后，可以重新检查随机情绪
                        if (hasCompletedBlinkCycle) {
                            canCheckRandomEmotion = true;
                        }
                        
                        displayImage(gImage_neutral);
                        ESP_LOGI(TAG, "自然情绪：眨眼结束，回到正常状态");
                        break;
                        
                    case NATURAL_RANDOM_EMOTION:
                        // 随机情绪结束，切换到正常状态
                        naturalState = NATURAL_NORMAL;
                        naturalStateChangeTime = currentTime;
                        naturalNextChangeTime = currentTime + pdMS_TO_TICKS(3000 + (rand() % 2000)); // 3-5秒随机
                        hasCompletedBlinkCycle = false; // 随机情绪后需要重新完成眨眼循环
                        lastRandomEmotionCheck = currentTime;
                        
                        displayImage(gImage_neutral);
                        ESP_LOGI(TAG, "自然情绪：随机情绪结束，回到正常状态");
                        break;
                }
            }
        };
        
        // 设置第一次自然情绪切换时间（3-5秒随机）
        naturalNextChangeTime = naturalStateChangeTime + pdMS_TO_TICKS(3000 + (rand() % 2000));
        lastRandomEmotionCheck = naturalStateChangeTime;
        
        while (true) {
            TickType_t currentTime = xTaskGetTickCount();
            
            // 检查当前设备状态
            bool isSpeaking = (app.GetDeviceState() == kDeviceStateSpeaking);
            bool isListening = (app.GetDeviceState() == kDeviceStateListening);
            bool isInDialog = (isSpeaking || isListening);
            board->SetInDialog(isInDialog);
            
            // 说话状态：显示情绪表情（最高优先级）
            if (isSpeaking) {
                // 显示当前情绪对应的图片
                std::string currentEmotion = board->GetCurrentEmotion();
                const uint8_t* emotionImage = getImageByEmotion(currentEmotion);
                displayImage(emotionImage);
                
                // 执行对应动作
                board->ExecuteEmotionAction(currentEmotion);
                
                // 只在刚开始说话时记录日志
                if (!wasSpeaking) {
                    ESP_LOGI(TAG, "说话状态：显示情绪 %s", currentEmotion.c_str());
                }
                wasSpeaking = true;
            }
            // 听话状态：显示neutral，如果刚说完话则重置
            else if (isListening) {
                // 如果刚从说话状态转到听话状态，执行说完话的重置逻辑
                if (wasSpeaking) {
                    wasSpeaking = false;
                    board->SetCurrentEmotion("neutral");
                    
                    // 重置自然情绪状态，准备在对话结束后使用
                    naturalState = NATURAL_NORMAL;
                    naturalStateChangeTime = currentTime;
                    naturalNextChangeTime = currentTime + pdMS_TO_TICKS(3000 + (rand() % 2000));
                    canCheckRandomEmotion = true;
                    hasCompletedBlinkCycle = false;
                    lastRandomEmotionCheck = currentTime;
                    
                    ESP_LOGI(TAG, "说话结束转为听话，重置为neutral");
                }
                
                // 只在刚进入听话状态时显示neutral
                if (!wasListening) {
                    displayImage(gImage_neutral);
                    ESP_LOGI(TAG, "听话状态：显示neutral");
                    wasListening = true;
                }
            }
            // 对话完全结束：启动自然情绪模块
            else if (!isInDialog) {
                // 重置对话状态标志
                wasListening = false;
                
                // 如果有残留的wasSpeaking标志，也要处理
                if (wasSpeaking) {
                    wasSpeaking = false;
                    board->SetCurrentEmotion("neutral");
                    displayImage(gImage_neutral);
                    ESP_LOGI(TAG, "对话完全结束，重置为neutral");
                }
                
                // 执行自然情绪模块
                processNaturalEmotion(currentTime);
            }
            
            // 短暂延时，避免CPU占用过高
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // 释放资源（实际上不会执行到这里，除非任务被外部终止）
        delete[] convertedData;
        vTaskDelete(NULL);
    }
    
    /* 
    // 原始的循环播放代码 - 已注释
    static void ImageSlideshowTask_Original(void* arg) {
        CompactWifiBoardS3Cam* board = static_cast<CompactWifiBoardS3Cam*>(arg);
        Display* display = board->GetDisplay();
        
        if (!display) {
            ESP_LOGE(TAG, "无法获取显示设备");
            vTaskDelete(NULL);
            return;
        }
        
        // 获取应用实例
        auto& app = Application::GetInstance();
        
        // 创建画布（如果不存在）
        if (!display->HasCanvas()) {
            display->CreateCanvas();
        }
        
        // 设置图片显示参数
        int imgWidth = 320;
        int imgHeight = 240;
        int x = 0;
        int y = 0;
        
        // 设置图片数组
        const uint8_t* imageArray[] = {
            gImage_output_0001,
            gImage_output_0002,
            gImage_output_0003,
            gImage_output_0004,
            gImage_output_0005,
            gImage_output_0006,
            gImage_output_0007,
            gImage_output_0008,
            gImage_output_0009,
            gImage_output_0010,
            gImage_output_0011,
            gImage_output_0012,
            gImage_output_0013,
            gImage_output_0014,
            gImage_output_0015,
        };
        const int totalImages = sizeof(imageArray) / sizeof(imageArray[0]);
        
        // 创建临时缓冲区用于字节序转换
        uint16_t* convertedData = new uint16_t[imgWidth * imgHeight];
        if (!convertedData) {
            ESP_LOGE(TAG, "无法分配内存进行图像转换");
            vTaskDelete(NULL);
            return;
        }
        
        // 先显示第一张图片
        int currentIndex = 0;
        const uint8_t* currentImage = imageArray[currentIndex];
        
        // 转换并显示第一张图片
        for (int i = 0; i < imgWidth * imgHeight; i++) {
            uint16_t pixel = ((uint16_t*)currentImage)[i];
            convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
        }
        display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
        ESP_LOGI(TAG, "初始显示图片");
        
        // 持续监控和处理图片显示
        TickType_t lastUpdateTime = xTaskGetTickCount();
        const TickType_t cycleInterval = pdMS_TO_TICKS(60); // 图片切换间隔60毫秒
        
        // 定义用于判断是否正在播放音频的变量
        bool isAudioPlaying = false;
        bool wasAudioPlaying = false;
        
        while (true) {
            // 检查是否正在播放音频 - 使用应用程序状态判断
            isAudioPlaying = (app.GetDeviceState() == kDeviceStateSpeaking);
            
            TickType_t currentTime = xTaskGetTickCount();
            
            // 如果正在播放音频且时间到了切换间隔
            if (isAudioPlaying && (currentTime - lastUpdateTime >= cycleInterval)) {
                // 更新索引到下一张图片
                currentIndex = (currentIndex + 1) % totalImages;
                currentImage = imageArray[currentIndex];
                
                // 转换并显示新图片
                for (int i = 0; i < imgWidth * imgHeight; i++) {
                    uint16_t pixel = ((uint16_t*)currentImage)[i];
                    convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                }
                display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
                
                // 更新上次更新时间
                lastUpdateTime = currentTime;
            }
            // 如果不在播放音频但上一次检查时在播放，或者当前不在第一张图片
            else if ((!isAudioPlaying && wasAudioPlaying) || (!isAudioPlaying && currentIndex != 0)) {
                // 切换回第一张图片
                currentIndex = 0;
                currentImage = imageArray[currentIndex];
                
                // 转换并显示第一张图片
                for (int i = 0; i < imgWidth * imgHeight; i++) {
                    uint16_t pixel = ((uint16_t*)currentImage)[i];
                    convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                }
                display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
                ESP_LOGI(TAG, "返回显示初始图片");
            }
            
            // 更新上一次音频播放状态
            wasAudioPlaying = isAudioPlaying;
            
            // 短暂延时，避免CPU占用过高
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // 释放资源（实际上不会执行到这里，除非任务被外部终止）
        delete[] convertedData;
        vTaskDelete(NULL);
    }
    */

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);
 

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_16_4,
                                        .icon_font = &font_awesome_16_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
#endif
                                    });
    }

    void InitializeCamera() {
        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;  
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
        camera_->SetHMirror(false);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
#elif CONFIG_IOT_PROTOCOL_MCP

#endif
    }

public:
    CompactWifiBoardS3Cam() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeIot();
        InitializeCamera();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        StartImageSlideshow();

    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
    
    // 设置当前情绪（供外部调用）
    virtual void SetEmotion(const char* emotion) override {
        if (emotion != nullptr) {
            current_emotion_ = std::string(emotion);
            ESP_LOGI(TAG, "设置当前情绪: %s", emotion);
        }
    }
    
    // 获取当前情绪
    std::string GetCurrentEmotion() const {
        return current_emotion_;
    }
    
    // 设置当前情绪（内部使用）
    void SetCurrentEmotion(const std::string& emotion) {
        current_emotion_ = emotion;
    }
    
    // 设置对话状态
    void SetInDialog(bool inDialog) {
        is_in_dialog_ = inDialog;
    }
    
    // 执行情绪对应的动作（有概率和随机性）
    void ExecuteEmotionAction(const std::string& emotion) {
        TickType_t currentTime = xTaskGetTickCount();
        
        // 防止动作执行过于频繁，至少间隔10秒
        if (currentTime - last_action_time_ < pdMS_TO_TICKS(10000)) {
            return;
        }
        
        // 20%概率执行动作
        if ((rand() % 100) <= 30) {
            return;
        }
        
        last_action_time_ = currentTime;
        
        // 根据情绪类型执行随机动作
        if (emotion == "laughing" || emotion == "funny" || emotion == "happy") {
            // 快乐类情绪的动作选项
            const char actions[] = {'a', 'c', 'e'}; // 动耳朵、摇摆、转圈
            const char* actionNames[] = {"动耳朵", "摇摆", "转圈"};
            int actionIndex = rand() % 3;
            send_uart_data(actions[actionIndex]);
            ESP_LOGI(TAG, "执行%s动作: %s", emotion.c_str(), actionNames[actionIndex]);
            
        } else if (emotion == "relaxed" || emotion == "sleepy" || emotion == "neutral") {
            // 放松类情绪的动作选项
            const char actions[] = {'6', 'h', 'b'}; // 点头、向前看、思考
            const char* actionNames[] = {"点头", "向前看", "思考"};
            int actionIndex = rand() % 3;
            send_uart_data(actions[actionIndex]);
            ESP_LOGI(TAG, "执行%s动作: %s", emotion.c_str(), actionNames[actionIndex]);
            
        } else if (emotion == "loving" || emotion == "kissy" || emotion == "winking") {
            // 爱意类情绪的动作选项
            int actionIndex = rand() % 3;
            if (actionIndex == 0) {
                send_uart_data('a');
                vTaskDelay(pdMS_TO_TICKS(500));
                send_uart_data('c');
                ESP_LOGI(TAG, "执行%s动作: 动耳朵+摇摆", emotion.c_str());
            } else if (actionIndex == 1) {
                send_uart_data('e');
                ESP_LOGI(TAG, "执行%s动作: 转圈", emotion.c_str());
            } else {
                send_uart_data('8');
                vTaskDelay(pdMS_TO_TICKS(300));
                send_uart_data('9');
                ESP_LOGI(TAG, "执行%s动作: 左右耳朵", emotion.c_str());
            }
            
        } else if (emotion == "sad" || emotion == "crying" || emotion == "embarrassed") {
            // 悲伤类情绪的动作选项
            int actionIndex = rand() % 3;
            if (actionIndex == 0) {
                send_uart_data('5');
                vTaskDelay(pdMS_TO_TICKS(500));
                send_uart_data('d');
                ESP_LOGI(TAG, "执行%s动作: 摇头+低头", emotion.c_str());
            } else if (actionIndex == 1) {
                send_uart_data('d');
                ESP_LOGI(TAG, "执行%s动作: 低头", emotion.c_str());
            } else {
                send_uart_data('5');
                ESP_LOGI(TAG, "执行%s动作: 摇头", emotion.c_str());
            }
            
        } else if (emotion == "surprised" || emotion == "thinking" || emotion == "confused") {
            // 思考类情绪的动作选项
            int actionIndex = rand() % 3;
            if (actionIndex == 0) {
                send_uart_data('b');
                ESP_LOGI(TAG, "执行%s动作: 思考", emotion.c_str());
            } else if (actionIndex == 1) {
                send_uart_data('g');
                vTaskDelay(pdMS_TO_TICKS(500));
                send_uart_data('f');
                ESP_LOGI(TAG, "执行%s动作: 左右看", emotion.c_str());
            } else {
                send_uart_data('a');
                vTaskDelay(pdMS_TO_TICKS(300));
                send_uart_data('b');
                ESP_LOGI(TAG, "执行%s动作: 动耳朵+思考", emotion.c_str());
            }
            
        } else if (emotion == "angry") {
            // 愤怒情绪的动作选项
            int actionIndex = rand() % 3;
            if (actionIndex == 0) {
                send_uart_data('5');
                vTaskDelay(pdMS_TO_TICKS(500));
                send_uart_data('d');
                ESP_LOGI(TAG, "执行%s动作: 摇头+低头", emotion.c_str());
            } else if (actionIndex == 1) {
                send_uart_data('5');
                ESP_LOGI(TAG, "执行%s动作: 摇头", emotion.c_str());
            } else {
                send_uart_data('0');
                ESP_LOGI(TAG, "执行%s动作: 停止", emotion.c_str());
            }
            
        } else {
            // 默认情绪的动作选项
            const char actions[] = {'6', 'b', 'a'};
            const char* actionNames[] = {"点头", "思考", "动耳朵"};
            int actionIndex = rand() % 3;
            send_uart_data(actions[actionIndex]);
            ESP_LOGI(TAG, "执行默认动作: %s", actionNames[actionIndex]);
        }
    }
};

DECLARE_BOARD(CompactWifiBoardS3Cam);
