#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

// --- UBAH: Pin Mikrofon INMP441 (I2S RX) ---
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_10 // Awalnya 4, diubah ke 10 (WS)
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_9  // Awalnya 5, diubah ke 9 (SCK)
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_11 // Awalnya 6, diubah ke 11 (SD)

// --- UBAH: Pin Speaker MAX98357A (I2S TX) ---
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_14 // Awalnya 7, diubah ke 14 (DIN)
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_12 // Awalnya 15, diubah ke 12 (BCLK)
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_13 // Awalnya 16, diubah ke 13 (LRC)

#else

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
// --- UBAH: Pin Sensor Sentuh TTP223 ---
// --- UBAH: Pin Sensor Sentuh TTP223 ---
#define BOOT_BUTTON_GPIO        GPIO_NUM_0  // Kembalikan ke tombol Boot bawaan ESP32
#define TOUCH_BUTTON_GPIO       GPIO_NUM_4  // Aktifkan sensor sentuh TTP223 di sini// Dimatikan (-1) agar tidak bentrok
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

// --- UBAH: Pin Layar OLED SSD1306 (I2C) ---
#define DISPLAY_SDA_PIN GPIO_NUM_8 // Awalnya 41, diubah ke 8 (SDA)
#define DISPLAY_SCL_PIN GPIO_NUM_7 // Awalnya 42, diubah ke 7 (SCL)
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "OLED display type is not selected"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true


// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

#endif // _BOARD_CONFIG_H_