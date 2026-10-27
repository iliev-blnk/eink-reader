#include "buttons.h"

#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_sleep.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "buttons";

#define POLL_MS 10
#define DEBOUNCE_MS 30  // a level must hold this long to count
#define LONG_MS 800

typedef struct {
    gpio_num_t pin;
    bool raw;        // ultima citire (true = apăsat)
    int stable_ms;   // de cât timp raw nu s-a schimbat
    bool down;       // starea după debounce
    int held_ms;
    bool long_sent;  // s-a raportat o apăsare lungă: fără apăsare scurtă la eliberare
    bool ignore;     // era apăsat la inițializare: ignorat până e eliberat
} btn_t;

static btn_t s_btn[BUTTON_COUNT];
static QueueHandle_t s_queue;
static TaskHandle_t s_task;

static void post(button_t b, press_t p)
{
    const button_event_t ev = {.button = b, .press = p};
    xQueueSend(s_queue, &ev, 0);  // se pierde când nu citește nimeni
}

static void poll_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(POLL_MS));
        for (int i = 0; i < BUTTON_COUNT; i++) {
            btn_t *b = &s_btn[i];
            bool raw = gpio_get_level(b->pin) == 0;
            if (raw != b->raw) {
                b->raw = raw;
                b->stable_ms = 0;
            } else if (b->stable_ms < DEBOUNCE_MS) {
                b->stable_ms += POLL_MS;
            }

            if (b->stable_ms >= DEBOUNCE_MS && raw != b->down) {
                b->down = raw;
                if (raw) {
                    b->held_ms = 0;
                    b->long_sent = false;
                } else {
                    if (!b->ignore && !b->long_sent) post(i, PRESS_SHORT);
                    b->ignore = false;
                }
            }
            if (b->down) {
                b->held_ms += POLL_MS;
                if (!b->ignore && !b->long_sent && b->held_ms >= LONG_MS) {
                    b->long_sent = true;
                    post(i, PRESS_LONG);
                }
            }
        }
    }
}

esp_err_t buttons_init(const gpio_num_t pins[BUTTON_COUNT])
{
    gpio_config_t io = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    for (int i = 0; i < BUTTON_COUNT; i++) io.pin_bit_mask |= BIT64(pins[i]);
    // Tot aici pinii se iau înapoi din domeniul RTC după o trezire din somnul adânc.
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "pins");

    for (int i = 0; i < BUTTON_COUNT; i++) {
        bool down = gpio_get_level(pins[i]) == 0;
        s_btn[i] = (btn_t){
            .pin = pins[i],
            .raw = down,
            .stable_ms = DEBOUNCE_MS,
            .down = down,
            .ignore = down,
        };
    }
    s_queue = xQueueCreate(8, sizeof(button_event_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    ESP_RETURN_ON_FALSE(xTaskCreate(poll_task, "buttons", 2560, NULL, 5, &s_task) == pdPASS, ESP_ERR_NO_MEM,
                        TAG, "task");
    return ESP_OK;
}

bool buttons_wait(button_event_t *ev, TickType_t timeout)
{
    return xQueueReceive(s_queue, ev, timeout) == pdTRUE;
}

bool buttons_any_down(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (gpio_get_level(s_btn[i].pin) == 0) return true;
    }
    return false;
}

esp_err_t buttons_enable_wakeup(void)
{
    if (s_task) {
        vTaskSuspend(s_task);  // pinii urmează să iasă din matricea GPIO
    }
    uint64_t mask = 0;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_num_t pin = s_btn[i].pin;
        mask |= BIT64(pin);
        // Pull-up-uri RTC; ESP-IDF ține pinii în somn când perifericele RTC
        // sunt oprite, deci nu costă un domeniu în plus.
        ESP_RETURN_ON_ERROR(rtc_gpio_init(pin), TAG, "rtc init %d", pin);
        ESP_RETURN_ON_ERROR(rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY), TAG, "rtc dir");
        ESP_RETURN_ON_ERROR(rtc_gpio_pulldown_dis(pin), TAG, "pulldown");
        ESP_RETURN_ON_ERROR(rtc_gpio_pullup_en(pin), TAG, "pullup");
    }
    return esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
}

int buttons_from_wake_mask(uint64_t mask)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (s_btn[i].pin != GPIO_NUM_NC && (mask & BIT64(s_btn[i].pin))) return i;
    }
    return -1;
}

const char *button_name(button_t b)
{
    static const char *const names[BUTTON_COUNT] = {"PREV", "NEXT", "MENU"};
    return b < BUTTON_COUNT ? names[b] : "?";
}
