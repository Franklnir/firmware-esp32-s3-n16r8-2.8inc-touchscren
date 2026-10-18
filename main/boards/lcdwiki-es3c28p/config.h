#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// Audio I2S
#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_6   // Mic data in (I2S_DI)
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_7   // LRC
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_8   // Speaker data out (I2S_DO)
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_1   // PA enable (low active)

// Audio & Touch I2C (Shared I2C0 bus)
#define AUDIO_CODEC_I2C_NUM      I2C_NUM_0
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_15
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_16
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

// Buttons & LED
#define BOOT_BUTTON_GPIO         GPIO_NUM_0
#define BUILTIN_LED_GPIO         GPIO_NUM_42  // WS2812 RGB LED

// Battery ADC
#define BATTERY_ADC_UNIT         ADC_UNIT_1
#define BATTERY_ADC_CHANNEL      ADC_CHANNEL_8 // GPIO 9

// Display SPI (ILI9341 / ILI9341V 2.8" IPS 240x320)
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_45
#define DISPLAY_RST_PIN          GPIO_NUM_NC  // Connected to EN/Reset
#define DISPLAY_SCK_PIN          GPIO_NUM_12
#define DISPLAY_DC_PIN           GPIO_NUM_46
#define DISPLAY_CS_PIN           GPIO_NUM_10
#define DISPLAY_MOSI_PIN         GPIO_NUM_11
#define DISPLAY_MIS0_PIN         GPIO_NUM_13
#define DISPLAY_SPI_SCLK_HZ      (40 * 1000 * 1000)

#define LCD_SPI_HOST             SPI3_HOST

#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH            240
#define DISPLAY_HEIGHT           320
#define DISPLAY_MIRROR_X         true
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_INVERT_COLOR     true
#define DISPLAY_RGB_ORDER        LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE         0

// Capacitive Touch (FT6336G)
#define TOUCH_I2C_ADDR           0x38
#define TOUCH_RST_PIN            GPIO_NUM_18
#define TOUCH_INT_PIN            GPIO_NUM_17

// MicroSD Slot (SDIO 4-Bit Hardware Bus)
#define SDMMC_PIN_CLK            GPIO_NUM_38
#define SDMMC_PIN_CMD            GPIO_NUM_40
#define SDMMC_PIN_D0             GPIO_NUM_39
#define SDMMC_PIN_D1             GPIO_NUM_41
#define SDMMC_PIN_D2             GPIO_NUM_48
#define SDMMC_PIN_D3             GPIO_NUM_47

#endif  // _BOARD_CONFIG_H_
