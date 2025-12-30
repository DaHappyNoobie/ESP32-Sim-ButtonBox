
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"

#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
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

// Compare contents of gamepad report struct. Return True if contents differ
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
    // Initialize button that will trigger HID reports
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

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

    while (1) {
        if (tud_mounted()) {
            if(compare_gamepad_report(&gamepad_report, &prevgamepad_report)) {
                copy_gamepad_report(&gamepad_report, &prevgamepad_report);
                app_send_hid_demo(gamepad_report);
            }
            if(!gpio_get_level(APP_BUTTON)){
                gamepad_report.buttons = GAMEPAD_BUTTON_0;
            } else {
                gamepad_report.buttons = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
