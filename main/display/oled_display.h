#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"
#include "face_engine.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <string>
#include <vector>

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    // Dipakai layout 128x32 (dan sebagian oleh destruktor)
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;

    // Layout 128x64: jam + tanggal di atas, wajah di tengah, transkrip di bawah
    FaceEngine* face_engine_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* transcript_label_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
    std::string last_time_text_;
    std::string last_date_text_;
    int line_height_ = 16;

    // Layar penuh untuk pesan sistem (pairing / konfigurasi WiFi / error)
    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* overlay_title_ = nullptr;
    lv_obj_t* overlay_code_ = nullptr;
    lv_obj_t* overlay_body_ = nullptr;
    lv_timer_t* overlay_timer_ = nullptr;
    std::vector<std::string> overlay_pages_;
    size_t overlay_page_ = 0;
    bool pairing_mode_ = false;

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetStatus(const char* status) override;

    void SetupUI_128x64();
    void SetupUI_128x32();

    void UpdateClock();
    void SetTranscript(const std::string& text);
    void ShowSystemMessage(const std::string& message);
    void HideOverlay();
    void NextOverlayPage();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
};

#endif  // OLED_DISPLAY_H
