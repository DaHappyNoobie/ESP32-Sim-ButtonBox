////////
//  ESP32 Sim Buttonbox - by DaHappyNoobie
//  Based on the tinyUSB USB HID example code from ESP-IDF.
//  This code instantiates a USB-HID gamepad profile from the TinyUSB stack, so that the ESP32-S3 shows up as a driverless game controller on PC.
//  GPIOs are used both directly on-chip as well as through a I²C GPIO expander for additional inputs.
//  Everything is handled as a simple sequential loop, as the use case here doesn't need high speed input reading and reporting.
//  Interrupt line of the GPIO expander is wired in, so interrupt capability can be added in firmware if needed.
////////
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include <pcf8575.h>

// I²C lines for GPIO Expander
#define PIN_I2C_SDA (GPIO_NUM_7)
#define PIN_I2C_SCL (GPIO_NUM_8)
#define PIN_I2C_INT (GPIO_NUM_9)
#define I2C_ADDR 0x20 //7-bit address of GPIO Expander
// MCU inputs
#define MOMENTARY_1A  (GPIO_NUM_1)
#define MOMENTARY_1B  (GPIO_NUM_2)
#define MOMENTARY_2A  (GPIO_NUM_42)
#define MOMENTARY_2B  (GPIO_NUM_41)
#define MOMENTARY_3A  (GPIO_NUM_40)
#define MOMENTARY_3B  (GPIO_NUM_39)
#define MOMENTARY_4A  (GPIO_NUM_48)
#define MOMENTARY_4B  (GPIO_NUM_47)
#define MOMENTARY_5A  (GPIO_NUM_21)
#define MOMENTARY_5B  (GPIO_NUM_14)
#define MOMENTARY_6A  (GPIO_NUM_13)
#define MOMENTARY_6B  (GPIO_NUM_12)
#define MOMENTARY_7A  (GPIO_NUM_11)
#define MOMENTARY_7B  (GPIO_NUM_10)
#define MOMENTARY_8A  (GPIO_NUM_18)
#define MOMENTARY_8B  (GPIO_NUM_17)
#define MOMENTARY_9A  (GPIO_NUM_16)
#define MOMENTARY_9B  (GPIO_NUM_15)
#define PUSHBUTTON    (GPIO_NUM_38)

#define GPIO_INPUT_MASK ((1ULL<<MOMENTARY_1A)|(1ULL<<MOMENTARY_1B)|(1ULL<<MOMENTARY_2A)|(1ULL<<MOMENTARY_2B)|(1ULL<<MOMENTARY_3A)|(1ULL<<MOMENTARY_3B)|(1ULL<<MOMENTARY_4A)|(1ULL<<MOMENTARY_4B)|(1ULL<<MOMENTARY_5A)|(1ULL<<MOMENTARY_5B)|(1ULL<<MOMENTARY_6A)|(1ULL<<MOMENTARY_6B)|(1ULL<<MOMENTARY_7A)|(1ULL<<MOMENTARY_7B)|(1ULL<<MOMENTARY_8A)|(1ULL<<MOMENTARY_8B)|(1ULL<<MOMENTARY_9A)|(1ULL<<MOMENTARY_9B)|(1ULL<<PUSHBUTTON))

static const char *TAG = "example";

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_GAMEPAD()
};

/**
 * @brief String descriptor
 */
const char* hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "Sim Button Box",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "Example HID interface",  // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

/**
 * @brief Gamepad report
 */
static hid_gamepad_report_t gamepad_report;

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
}

/********* Application ***************/

// Copy contents of gamepad report struct.
static void copy_gamepad_report(hid_gamepad_report_t* source, hid_gamepad_report_t* destination)
{
    destination->x = source->x;
    destination->y = source->y;
    destination->z = source->z;
    destination->rz = source->rz;
    destination->rx = source->rx;
    destination->ry = source->ry;
    destination->hat = source->hat;
    destination->buttons = source->buttons;
}

// Compare contents of gamepad report struct. Return True if contents differ, so we don't send the same report repeatedly
static bool compare_gamepad_report(hid_gamepad_report_t* rep_a, hid_gamepad_report_t* rep_b)
{
    uint8_t count = 0;
    count += rep_a->x != rep_b->x;
    count += rep_a->y != rep_b->y;
    count += rep_a->z != rep_b->z;
    count += rep_a->rz != rep_b->rz;
    count += rep_a->rx != rep_b->rx;
    count += rep_a->ry != rep_b->ry;
    count += rep_a->hat != rep_b->hat;
    count += rep_a->buttons != rep_b->buttons;
    if(count > 0) return true;
    else return false;
}

static void app_send_hid_demo(hid_gamepad_report_t report)
{
    // Gamepad output
    ESP_LOGI(TAG, "Sending Gamepad report");
    tud_hid_report(0, &report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(1));
}

void app_main(void)
{
    // Initialize buttons
    const gpio_config_t button_config = {
        .pin_bit_mask = GPIO_INPUT_MASK,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        // Pullups are external, so no need to enable the internal ones
        .pull_up_en = false,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));

    // Initialize IO expander
    ESP_ERROR_CHECK(i2cdev_init());
    i2c_dev_t pcf8575;
    memset(&pcf8575, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8575_init_desc(&pcf8575, I2C_ADDR, 0, PIN_I2C_SDA, PIN_I2C_SCL));
    uint16_t gpioExpanderPortVal = 0xFFFF;

    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = hid_configuration_descriptor, // HID configuration descriptor for full-speed and high-speed are the same
        .hs_configuration_descriptor = hid_configuration_descriptor,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = hid_configuration_descriptor,
#endif // TUD_OPT_HIGH_SPEED
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");

    // Initialize gamepad state to all 0
    gamepad_report.x = 0;
    gamepad_report.y = 0;
    gamepad_report.z = 0;
    gamepad_report.rz = 0;
    gamepad_report.rx = 0;
    gamepad_report.ry = 0;
    gamepad_report.hat = 0;
    gamepad_report.buttons = 0;
    static hid_gamepad_report_t prevgamepad_report;
    copy_gamepad_report(&gamepad_report, &prevgamepad_report);

    // Main program loop
    while (1) {
        if (tud_mounted()) {
            // If the new report differs form the previous one, send it through USB.
            if(compare_gamepad_report(&gamepad_report, &prevgamepad_report)) {
                copy_gamepad_report(&gamepad_report, &prevgamepad_report);
                app_send_hid_demo(gamepad_report);
            }
            // Get IO states and build report
            gamepad_report.buttons = 0;
            if(!gpio_get_level(MOMENTARY_1A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_0;
            } if(!gpio_get_level(MOMENTARY_1B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_1;
            } if(!gpio_get_level(MOMENTARY_2A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_2;
            } if(!gpio_get_level(MOMENTARY_2B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_3;
            } if(!gpio_get_level(MOMENTARY_3A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_4;
            } if(!gpio_get_level(MOMENTARY_3B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_5;
            } if(!gpio_get_level(MOMENTARY_4A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_6;
            } if(!gpio_get_level(MOMENTARY_4B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_7;
            } if(!gpio_get_level(MOMENTARY_5A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_8;
            } if(!gpio_get_level(MOMENTARY_5B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_9;
            } if(!gpio_get_level(MOMENTARY_6A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_10;
            } if(!gpio_get_level(MOMENTARY_6B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_11;
            } if(!gpio_get_level(MOMENTARY_7A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_12;
            } if(!gpio_get_level(MOMENTARY_7B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_13;
            } if(!gpio_get_level(MOMENTARY_8A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_14;
            } if(!gpio_get_level(MOMENTARY_8B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_15;
            } if(!gpio_get_level(MOMENTARY_9A)){
                gamepad_report.buttons += GAMEPAD_BUTTON_16;
            } if(!gpio_get_level(MOMENTARY_9B)){
                gamepad_report.buttons += GAMEPAD_BUTTON_17;
            } if(!gpio_get_level(PUSHBUTTON)){
                gamepad_report.buttons += GAMEPAD_BUTTON_18;
            }
            // Poll GPIO Expander and complete report
            pcf8575_port_read(&pcf8575, &gpioExpanderPortVal);
            ESP_LOGI("IOEXP","Port read : %d", gpioExpanderPortVal);
            gamepad_report.buttons += (((uint32_t)gpioExpanderPortVal & 0x000000FF) << 24);
        }
        // Some dead time to allow other OS tasks to run, like the USB stack
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
