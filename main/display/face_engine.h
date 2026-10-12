#pragma once

#include <lvgl.h>
#include <stdint.h>

#include <atomic>

enum class FaceState { Idle, Listening, Speaking };

class FaceEngine {
public:
    ~FaceEngine();

    // Harus dipanggil dengan lock LVGL/display sudah dipegang.
    void Init(lv_obj_t* parent);

    // Aman dipanggil dari task lain (hanya menulis enum atomic).
    void SetState(FaceState state);

    // Dipanggil oleh timer LVGL (sudah di dalam lock LVGL).
    void Update();

private:
    void IdleBehavior(int base_eye_height);
    void ListeningBehavior(int base_eye_height);
    void SpeakingBehavior(int eye_height);

    // Helper agar posisi/ukuran mata dan mulut konsisten di semua state.
    void SetEyes(int left_h, int right_h, int dx, int dy);
    void SetMouth(int w, int h, int radius, int dx, int dy);
    int EyeWidth() const { return eye_size_ * 65 / 100; }

    lv_obj_t* container_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    std::atomic<FaceState> state_{FaceState::Idle};

    int eye_size_ = 30;

    int idle_move_offset_x_ = 0;
    int idle_move_offset_y_ = 0;

    int blink_phase_ = 0;

    uint32_t speak_last_update_ = 0;
    int speak_mouth_target_ = 4;
    int speak_mouth_current_ = 4;
};
