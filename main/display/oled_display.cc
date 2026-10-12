#include "oled_display.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <material_symbols.h>
#include <noto_emoji.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

// ---- Teks yang tampil di layar (ubah sesuai selera) ----
#define PAIRING_TITLE "PAIRING"
#define PAIRING_HINT "Isi di panel web"
#define OVERLAY_PAGE_MS 3000   // ganti halaman pesan sistem yang panjang
#define OVERLAY_PAGE_CHARS 44  // ~4 baris x ~11-17 karakter per halaman

static const char* const kDayNames[7] = {"Min", "Sen", "Sel", "Rab", "Kam", "Jum", "Sab"};
static const char* const kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr", "Mei", "Jun",
                                            "Jul", "Agu", "Sep", "Okt", "Nov", "Des"};

// Cari deretan angka >= 4 digit (kode pairing) di dalam pesan.
static std::string ExtractCode(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            size_t j = i;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
                j++;
            }
            if (j - i >= 4) {
                return s.substr(i, j - i);
            }
            i = j;
        } else {
            i++;
        }
    }
    return "";
}

// Pecah teks panjang jadi beberapa halaman per kata.
static std::vector<std::string> SplitPages(const std::string& s, size_t max_chars) {
    std::vector<std::string> pages;
    std::string cur;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(' ', i);
        std::string word = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = (j == std::string::npos) ? s.size() : j + 1;
        if (word.empty()) {
            continue;
        }
        while (word.size() > max_chars) {  // kata sangat panjang -> potong
            if (!cur.empty()) {
                pages.push_back(cur);
                cur.clear();
            }
            pages.push_back(word.substr(0, max_chars));
            word.erase(0, max_chars);
        }
        if (!cur.empty() && cur.size() + 1 + word.size() > max_chars) {
            pages.push_back(cur);
            cur.clear();
        }
        if (!cur.empty()) {
            cur += ' ';
        }
        cur += word;
    }
    if (!cur.empty()) {
        pages.push_back(cur);
    }
    if (pages.empty()) {
        pages.push_back("");
    }
    return pages;
}

void OledDisplay::SetStatus(const char* status) {
    // Status text tidak ditampilkan langsung; status dipetakan ke ekspresi wajah
    // dan menentukan kapan layar pairing/pesan sistem ditutup.
    if (face_engine_ == nullptr || status == nullptr)
        return;

    DisplayLockGuard lock(this);

    if (strcmp(status, Lang::Strings::ACTIVATION) == 0) {
        // Application::Alert(ACTIVATION, ...) memanggil SetStatus dulu, lalu
        // SetChatMessage("system", pesan) -> pesan itulah yang berisi kode.
        pairing_mode_ = true;
        face_engine_->SetState(FaceState::Idle);
    } else if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        pairing_mode_ = false;
        HideOverlay();
        SetTranscript("");
        face_engine_->SetState(FaceState::Listening);
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        pairing_mode_ = false;
        HideOverlay();
        face_engine_->SetState(FaceState::Speaking);
    } else if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        pairing_mode_ = false;
        HideOverlay();
        SetTranscript("");
        face_engine_->SetState(FaceState::Idle);
    } else {
        // Status lain (connecting, upgrading, jam "HH:MM" dari status bar, dll)
        face_engine_->SetState(FaceState::Idle);
    }
}

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    const int W = LV_HOR_RES;
    const int H = LV_VER_RES;
    const int lh = std::clamp<int>(lv_font_get_line_height(text_font), 12, 18);
    line_height_ = lh;

    auto bare = [](lv_obj_t* o) {
        lv_obj_set_style_pad_all(o, 0, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    };

    /* Container utama (posisi absolut, tanpa flex) */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, W, H);
    lv_obj_set_pos(container_, 0, 0);
    bare(container_);

    /* Baris atas: jam (kiri) dan tanggal (kanan) */
    time_label_ = lv_label_create(container_);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_set_pos(time_label_, 2, 0);

    date_label_ = lv_label_create(container_);
    lv_obj_set_width(date_label_, W - 44);
    lv_obj_set_style_text_align(date_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(date_label_, "");
    lv_obj_set_pos(date_label_, 42, 0);

    /* Tengah: wajah */
    face_engine_ = new FaceEngine();
    face_engine_->Init(container_, 0, lh, W, H - 2 * lh);

    /* Baris bawah: transkrip (scroll horizontal kalau panjang) */
    transcript_label_ = lv_label_create(container_);
    lv_obj_set_width(transcript_label_, W - 4);
    lv_obj_set_pos(transcript_label_, 2, H - lh);
    lv_label_set_long_mode(transcript_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(transcript_label_, "");

    static lv_anim_t transcript_anim;
    lv_anim_init(&transcript_anim);
    lv_anim_set_delay(&transcript_anim, 800);
    lv_anim_set_repeat_count(&transcript_anim, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(transcript_label_, &transcript_anim, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(transcript_label_, lv_anim_speed_clamped(40, 300, 60000),
                                   LV_PART_MAIN);

    /* Overlay layar penuh: pairing / pesan sistem (awalnya tersembunyi) */
    overlay_ = lv_obj_create(screen);
    lv_obj_set_size(overlay_, W, H);
    lv_obj_set_pos(overlay_, 0, 0);
    bare(overlay_);

    overlay_title_ = lv_label_create(overlay_);
    lv_obj_set_width(overlay_title_, W);
    lv_obj_set_style_text_align(overlay_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(overlay_title_, 0, 0);
    lv_label_set_text(overlay_title_, "");

    overlay_code_ = lv_label_create(overlay_);
    lv_obj_set_width(overlay_code_, W);
    lv_obj_set_style_text_align(overlay_code_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(overlay_code_, 3, 0);
    lv_obj_set_pos(overlay_code_, 0, lh + 2);
    lv_label_set_text(overlay_code_, "");

    overlay_body_ = lv_label_create(overlay_);
    lv_obj_set_width(overlay_body_, W - 4);
    lv_label_set_long_mode(overlay_body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(overlay_body_, 2, 0);
    lv_label_set_text(overlay_body_, "");

    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    /* Jam: update tiap 1 detik (label hanya diubah kalau teksnya berubah) */
    clock_timer_ = lv_timer_create(
        [](lv_timer_t* t) {
            static_cast<OledDisplay*>(lv_timer_get_user_data(t))->UpdateClock();
        },
        1000, this);
    UpdateClock();
}

void OledDisplay::UpdateClock() {
    if (time_label_ == nullptr || date_label_ == nullptr)
        return;

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char time_buf[8];
    char date_buf[24];

    if (tm_now.tm_year < 2025 - 1900) {
        // Waktu belum tersinkron (belum dapat waktu dari server/SNTP)
        snprintf(time_buf, sizeof(time_buf), "--:--");
        date_buf[0] = '\0';
    } else {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        snprintf(date_buf, sizeof(date_buf), "%s %d %s", kDayNames[tm_now.tm_wday],
                 tm_now.tm_mday, kMonthNames[tm_now.tm_mon]);
    }

    if (last_time_text_ != time_buf) {
        last_time_text_ = time_buf;
        lv_label_set_text(time_label_, time_buf);
    }
    if (last_date_text_ != date_buf) {
        last_date_text_ = date_buf;
        lv_label_set_text(date_label_, date_buf);
    }
}

void OledDisplay::SetTranscript(const std::string& text) {
    if (transcript_label_ == nullptr)
        return;
    lv_anim_delete(transcript_label_, nullptr);  // reset posisi scroll
    lv_label_set_text(transcript_label_, text.c_str());
}

void OledDisplay::ShowSystemMessage(const std::string& message) {
    if (overlay_ == nullptr)
        return;

    if (overlay_timer_ != nullptr) {
        lv_timer_delete(overlay_timer_);
        overlay_timer_ = nullptr;
    }
    overlay_pages_.clear();
    overlay_page_ = 0;

    const int lh = line_height_;
    const std::string code = pairing_mode_ ? ExtractCode(message) : std::string();

    if (!code.empty()) {
        // Tampilan pairing: judul, kode, petunjuk
        lv_label_set_text(overlay_title_, PAIRING_TITLE);
        lv_label_set_text(overlay_code_, code.c_str());
        lv_label_set_text(overlay_body_, PAIRING_HINT);
        lv_obj_remove_flag(overlay_title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(overlay_code_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_align(overlay_body_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(overlay_body_, 2, 2 * lh + 6);
    } else {
        // Pesan sistem biasa (WiFi config, error, dll): tampil per halaman
        lv_obj_add_flag(overlay_title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(overlay_code_, LV_OBJ_FLAG_HIDDEN);
        overlay_pages_ = SplitPages(message, OVERLAY_PAGE_CHARS);
        lv_label_set_text(overlay_body_, overlay_pages_[0].c_str());
        lv_obj_set_style_text_align(overlay_body_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(overlay_body_, 2, 0);

        if (overlay_pages_.size() > 1) {
            overlay_timer_ = lv_timer_create(
                [](lv_timer_t* t) {
                    static_cast<OledDisplay*>(lv_timer_get_user_data(t))->NextOverlayPage();
                },
                OVERLAY_PAGE_MS, this);
        }
    }

    lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_);
}

void OledDisplay::NextOverlayPage() {
    if (overlay_body_ == nullptr || overlay_pages_.size() < 2)
        return;
    overlay_page_ = (overlay_page_ + 1) % overlay_pages_.size();
    lv_label_set_text(overlay_body_, overlay_pages_[overlay_page_].c_str());
}

void OledDisplay::HideOverlay() {
    if (overlay_timer_ != nullptr) {
        lv_timer_delete(overlay_timer_);
        overlay_timer_ = nullptr;
    }
    overlay_pages_.clear();
    overlay_page_ = 0;
    if (overlay_ != nullptr) {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_1);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_1);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Note: SetupUI() dipanggil oleh Application::Initialize(), bukan di konstruktor,
    // supaya objek LVGL dibuat setelah display siap sepenuhnya.
}

void OledDisplay::SetupUI() {
    // Cegah pemanggilan ganda
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Tandai SetupUI sudah dipanggil
    if (height_ == 64) {
        SetupUI_128x64();
    } else {
        SetupUI_128x32();
    }
}

OledDisplay::~OledDisplay() {
    {
        DisplayLockGuard lock(this);

        // Hentikan semua timer sebelum objek LVGL-nya dihapus.
        if (clock_timer_ != nullptr) {
            lv_timer_delete(clock_timer_);
            clock_timer_ = nullptr;
        }
        if (overlay_timer_ != nullptr) {
            lv_timer_delete(overlay_timer_);
            overlay_timer_ = nullptr;
        }
        if (face_engine_ != nullptr) {
            delete face_engine_;
            face_engine_ = nullptr;
        }

        // Semua objek lain adalah anak dari container_ / overlay_, jadi ikut terhapus.
        if (overlay_ != nullptr) {
            lv_obj_del(overlay_);
        }
        if (container_ != nullptr) {
            lv_obj_del(container_);
        }

        overlay_ = overlay_title_ = overlay_code_ = overlay_body_ = nullptr;
        time_label_ = date_label_ = transcript_label_ = nullptr;
        top_bar_ = status_bar_ = content_ = content_left_ = content_right_ = nullptr;
        container_ = side_bar_ = emotion_label_ = chat_message_label_ = nullptr;
        status_label_ = notification_label_ = network_label_ = nullptr;
        mute_label_ = battery_label_ = nullptr;
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (content == nullptr) {
        content = "";
    }

    // Ganti newline dengan spasi
    std::string text = content;
    std::replace(text.begin(), text.end(), '\n', ' ');

    if (overlay_ != nullptr) {
        // ---- Layout 128x64 ----
        if (role != nullptr && strcmp(role, "system") == 0) {
            // Pesan sistem (kode pairing, WiFi config, error) -> layar penuh
            if (text.empty()) {
                HideOverlay();
            } else {
                ShowSystemMessage(text);
            }
        } else {
            // "user" = hasil STT saat listening, "assistant" = kalimat TTS saat bicara
            SetTranscript(text);
        }
        return;
    }

    // ---- Layout 128x32: perilaku asli ----
    if (chat_message_label_ == nullptr) {
        return;
    }

    lv_anim_delete(chat_message_label_, nullptr);
    if (content_right_ == nullptr) {
        lv_label_set_text(chat_message_label_, text.c_str());
    } else {
        if (text.empty()) {
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(chat_message_label_, text.c_str());
            lv_obj_remove_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void OledDisplay::SetupUI_128x32() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_column(container_, 0, 0);

    /* Emotion label on the left side */
    content_ = lv_obj_create(container_);
    lv_obj_set_size(content_, 32, 32);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);

    /* Right side */
    side_bar_ = lv_obj_create(container_);
    lv_obj_set_size(side_bar_, width_ - 32, 32);
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(side_bar_, 0, 0);
    lv_obj_set_style_border_width(side_bar_, 0, 0);
    lv_obj_set_style_radius(side_bar_, 0, 0);
    lv_obj_set_style_pad_row(side_bar_, 0, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(side_bar_);
    lv_obj_set_size(status_bar_, width_ - 32, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_obj_set_style_pad_left(status_label_, 2, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_pad_left(notification_label_, 2, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    chat_message_label_ = lv_label_create(side_bar_);
    lv_obj_set_size(chat_message_label_, width_ - 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(chat_message_label_, 2, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(chat_message_label_, "");

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);
}

void OledDisplay::SetEmotion(const char* emotion) {
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const char* utf8 = noto_emoji_get_utf8(emotion);
    const lv_font_t* emotion_font = lvgl_theme->emoji_font()->font();
    if (utf8 == nullptr) {
        utf8 = material_symbols_get_utf8(emotion);
        emotion_font = lvgl_theme->large_icon_font()->font();
    }
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;  // layout 128x64 (wajah) tidak memakai emotion label
    }
    if (utf8 != nullptr) {
        lv_obj_set_style_text_font(emotion_label_, emotion_font, 0);
        lv_label_set_text(emotion_label_, utf8);
    } else {
        lv_obj_set_style_text_font(emotion_label_, lvgl_theme->emoji_font()->font(), 0);
        lv_label_set_text(emotion_label_, NOTO_EMOJI_NEUTRAL);
    }
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
}

void OledDisplay::SetPowerSaveMode(bool on) {
    if (panel_) {
        Settings settings("wifi", false);
        if (settings.GetBool("power_save_display_off", false)) {
            esp_lcd_panel_disp_on_off(panel_, !on);
        }
    }
    LvglDisplay::SetPowerSaveMode(on);
}
