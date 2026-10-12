#include "face_engine.h"

#include <algorithm>

#include <esp_log.h>
#include <esp_random.h>

#define TAG "FaceEngine"

// Pada OLED monokrom lewat esp_lvgl_port, hitam = piksel menyala.
// Kalau wajah tidak terlihat / terbalik, ganti ke lv_color_white().
#define FACE_COLOR lv_color_black()

static inline int RandInt(int n) {
    return static_cast<int>(esp_random() % static_cast<uint32_t>(n));
}

FaceEngine::~FaceEngine() {
    // Objek LVGL (container_, mata, mulut) dimiliki parent-nya, jadi tidak dihapus di sini.
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void FaceEngine::Init(lv_obj_t* parent, int x, int y, int width, int height) {
    face_w_ = width;
    face_h_ = height;

    eye_h_ = std::max(6, height / 2);
    eye_w_ = std::max(4, eye_h_ * 7 / 10);
    eye_dx_ = width * 21 / 100;
    eye_dy_ = -(height * 10 / 100);
    mouth_dy_ = height * 32 / 100;
    mouth_max_ = std::max(4, height / 4);

    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, width, height);
    lv_obj_set_pos(container_, x, y);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    left_eye_ = lv_obj_create(container_);
    right_eye_ = lv_obj_create(container_);
    mouth_ = lv_obj_create(container_);

    for (lv_obj_t* obj : {left_eye_, right_eye_, mouth_}) {
        lv_obj_set_style_bg_color(obj, FACE_COLOR, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_set_style_radius(left_eye_, 2, 0);
    lv_obj_set_style_radius(right_eye_, 2, 0);

    // Pose awal = idle
    SetEyes(eye_h_, eye_h_, 0, 0);
    SetMouth(face_w_ * 11 / 100, std::max(2, face_h_ / 8), 2, 0, mouth_dy_);

    timer_ = lv_timer_create(
        [](lv_timer_t* t) {
            auto face = static_cast<FaceEngine*>(lv_timer_get_user_data(t));
            face->Update();
        },
        60, this);  // ~16 FPS
}

void FaceEngine::SetState(FaceState state) { state_.store(state); }

void FaceEngine::SetEyes(int left_h, int right_h, int dx, int dy) {
    left_h = std::max(1, left_h);
    right_h = std::max(1, right_h);

    lv_obj_set_size(left_eye_, eye_w_, left_h);
    lv_obj_set_size(right_eye_, eye_w_, right_h);

    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_dx_ + dx, eye_dy_ + dy);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_dx_ + dx, eye_dy_ + dy);
}

void FaceEngine::SetMouth(int w, int h, int radius, int dx, int dy) {
    lv_obj_set_size(mouth_, std::max(1, w), std::max(1, h));
    lv_obj_set_style_radius(mouth_, radius, 0);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, dx, dy);
}

void FaceEngine::IdleBehavior(int base_eye_height) {
    if (RandInt(40) == 0) {
        idle_move_offset_x_ = RandInt(5) - 2;
        idle_move_offset_y_ = RandInt(3) - 1;
    }

    SetEyes(base_eye_height, base_eye_height, idle_move_offset_x_, idle_move_offset_y_);
    SetMouth(face_w_ * 11 / 100, std::max(2, face_h_ / 8), 2, idle_move_offset_x_,
             mouth_dy_ + idle_move_offset_y_);
}

void FaceEngine::ListeningBehavior(int base_eye_height) {
    // Mata kiri sedikit lebih kecil (ekspresi asimetris).
    SetEyes(base_eye_height - std::max(1, eye_h_ / 10), base_eye_height, 0, 0);
    SetMouth(face_w_ / 16, 2, 2, -face_w_ / 32, mouth_dy_);
}

void FaceEngine::SpeakingBehavior(int eye_height) {
    const uint32_t now = lv_tick_get();

    SetEyes(eye_height, eye_height, 0, 0);

    if (now - speak_last_update_ > static_cast<uint32_t>(90 + RandInt(60))) {
        speak_last_update_ = now;

        const int r = RandInt(100);
        if (r < 20)
            speak_mouth_target_ = std::max(2, mouth_max_ / 4);
        else if (r < 50)
            speak_mouth_target_ = mouth_max_ / 2;
        else if (r < 80)
            speak_mouth_target_ = mouth_max_ * 3 / 4;
        else
            speak_mouth_target_ = mouth_max_;
    }

    // Transisi halus, maksimal 2 px per frame
    speak_mouth_current_ += std::clamp(speak_mouth_target_ - speak_mouth_current_, -2, 2);

    SetMouth(face_w_ / 10, speak_mouth_current_, LV_RADIUS_CIRCLE, 0, mouth_dy_);
}

void FaceEngine::Update() {
    if (container_ == nullptr)
        return;

    if (blink_phase_ == 0 && RandInt(120) == 0) {
        blink_phase_ = 1;
    }

    int eye_height = eye_h_;

    switch (blink_phase_) {
        case 1:  // menutup
            eye_height = std::max(1, eye_h_ / 4);
            blink_phase_ = 2;
            break;
        case 2:  // tertutup
            eye_height = 1;
            blink_phase_ = 3;
            break;
        case 3:  // membuka
            eye_height = std::max(1, eye_h_ / 2);
            blink_phase_ = 0;
            break;
        default:
            break;
    }

    switch (state_.load()) {
        case FaceState::Idle:
            IdleBehavior(eye_height);
            break;
        case FaceState::Listening:
            ListeningBehavior(eye_height);
            break;
        case FaceState::Speaking:
            SpeakingBehavior(eye_height);
            break;
    }
}
