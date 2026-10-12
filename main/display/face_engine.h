#pragma once

#include <lvgl.h>
#include <stdint.h>

#include <atomic>

enum class FaceState { Idle, Listening, Speaking };

class FaceEngine {
public:
    ~FaceEngine();

    // Dipanggil dengan lock LVGL/display sudah dipegang.
    // Wajah digambar di area (x, y, width, height) pada layar; semua ukuran
    // mata/mulut diturunkan dari width & height, jadi area bisa diubah bebas.
    void Init(lv_obj_t* parent, int x, int y, int width, int height);

    // Aman dipanggil dari task lain (hanya menulis enum atomic).
    void SetState(FaceState state);

    // Dipanggil oleh timer LVGL (sudah di dalam lock LVGL).
    void Update();

private:
    void IdleBehavior(int base_eye_height);
    void ListeningBehavior(int base_eye_height);
    void SpeakingBehavior(int eye_height);

    void SetEyes(int left_h, int right_h, int dx, int dy);
    void SetMouth(int w, int h, int radius, int dx, int dy);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    std::atomic<FaceState> state_{FaceState::Idle};

    // Geometri (dihitung di Init dari ukuran area wajah)
    int face_w_ = 128;
    int face_h_ = 32;
    int eye_h_ = 16;
    int eye_w_ = 11;
    int eye_dx_ = 26;
    int eye_dy_ = -3;
    int mouth_dy_ = 10;
    int mouth_max_ = 8;

    int idle_move_offset_x_ = 0;
    int idle_move_offset_y_ = 0;

    int blink_phase_ = 0;

    uint32_t speak_last_update_ = 0;
    int speak_mouth_target_ = 4;
    int speak_mouth_current_ = 4;
};
