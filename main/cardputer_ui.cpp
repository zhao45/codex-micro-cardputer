#include "cardputer_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static constexpr int LCD_WIDTH = 240;
static constexpr int LCD_HEIGHT = 135;
static constexpr uint16_t BLACK = 0x0000;
static constexpr uint16_t WHITE = 0xFFFF;
static constexpr uint16_t GRAY = 0x8410;
static constexpr uint16_t DARK = 0x2104;
static constexpr uint16_t GREEN = 0x07E0;
static constexpr uint16_t CYAN = 0x07FF;

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_framebuffer;
static SemaphoreHandle_t s_mutex;
static cardputer_ui_state_t s_state;
static bool s_dirty;

static uint16_t wire_color(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static void pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x < LCD_WIDTH && (unsigned)y < LCD_HEIGHT) {
        s_framebuffer[y * LCD_WIDTH + x] = wire_color(color);
    }
}

static void fill_rect(int x, int y, int width, int height, uint16_t color)
{
    for (int yy = y; yy < y + height; ++yy) {
        for (int xx = x; xx < x + width; ++xx) {
            pixel(xx, yy, color);
        }
    }
}

static const uint8_t *glyph(char character)
{
    static const uint8_t font[][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
        {31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},
        {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},
        {30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
        {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    };
    static const uint8_t blank[7] = {};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t colon[7] = {0,4,0,0,4,0,0};
    static const uint8_t percent[7] = {17,2,4,8,17,0,0};
    static const uint8_t slash[7] = {1,2,2,4,8,8,16};
    if (character >= '0' && character <= '9') return font[character - '0'];
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character >= 'A' && character <= 'Z') return font[10 + character - 'A'];
    if (character == '-') return dash;
    if (character == ':') return colon;
    if (character == '%') return percent;
    if (character == '/') return slash;
    return blank;
}

static void text(int x, int y, const char *value, uint16_t color, int scale = 1)
{
    while (*value != '\0') {
        const uint8_t *rows = glyph(*value++);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if (rows[row] & (1U << (4 - column))) {
                    fill_rect(x + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static uint16_t agent_color(const cardputer_ui_agent_t &agent)
{
    if (!agent.known || agent.brightness <= 0.01f) return DARK;
    const float b = agent.brightness > 1.0f ? 1.0f : agent.brightness;
    const uint8_t r = (uint8_t)(((agent.color_rgb >> 16) & 0xff) * b);
    const uint8_t g = (uint8_t)(((agent.color_rgb >> 8) & 0xff) * b);
    const uint8_t bl = (uint8_t)((agent.color_rgb & 0xff) * b);
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (bl >> 3));
}

static void render_status(const cardputer_ui_state_t &state)
{
    text(6, 5, "CODEX MICRO", WHITE, 2);
    text(165, 10, "CARDPUTER", CYAN);
    const char *transport = state.active_transport == CARDPUTER_TRANSPORT_USB ? "CTRL USB" :
                            state.active_transport == CARDPUTER_TRANSPORT_BLE ? "CTRL BLE" :
                            "CTRL NONE";
    text(6, 26, transport,
         state.active_transport == CARDPUTER_TRANSPORT_NONE ? GRAY : GREEN);
    char battery[16];
    if (state.battery_valid) {
        snprintf(battery, sizeof(battery), "BAT %u%%", state.battery_percentage);
    } else {
        snprintf(battery, sizeof(battery), "BAT --%%");
    }
    text(150, 26, battery, WHITE);

    for (uint8_t index = 0; index < CODEX_AGENT_COUNT; ++index) {
        const int column = index / 3;
        const int row = index % 3;
        const int x = 6 + column * 117;
        const int y = 48 + row * 20;
        const cardputer_ui_agent_t &agent = state.agents[index];
        const uint16_t color = agent_color(agent);
        fill_rect(x, y, 8, 8, color);
        char label[20];
        const char *status = !agent.known ? "UNKNOWN" :
                             (agent.brightness > 0.01f ? "ON" : "OFF");
        snprintf(label, sizeof(label), "AGENT %u %s", index + 1, status);
        text(x + 13, y, label, index == state.selected_agent ? CYAN : GRAY);
    }
    text(6, 118, "SPACE KEY MAP", GRAY);
}

static void render_key_map(void)
{
    text(6, 5, "CODEX KEY MAP", WHITE, 2);
    text(6, 27, "ENTER  SEND", CYAN);
    text(6, 40, "M      MIC HOLD", CYAN);
    text(6, 53, "Y      APPROVE", CYAN);
    text(6, 66, "N      DECLINE", CYAN);
    text(126, 27, "F      FAST", CYAN);
    text(126, 40, "TAB    FORK", CYAN);
    text(126, 53, "1-6    AGENTS", CYAN);
    text(126, 66, "SPACE  BACK", CYAN);
}

static void display_task(void *)
{
    while (true) {
        cardputer_ui_state_t state;
        bool redraw;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        state = s_state;
        redraw = s_dirty;
        s_dirty = false;
        xSemaphoreGive(s_mutex);
        if (redraw) {
            fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, BLACK);
            state.page == CARDPUTER_UI_STATUS ? render_status(state) : render_key_map();
            esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, s_framebuffer);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern "C" esp_err_t cardputer_ui_start(void)
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_35;
    bus.miso_io_num = GPIO_NUM_NC;
    bus.sclk_io_num = GPIO_NUM_36;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO),
                        "cardputer_ui", "initialize LCD SPI");

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_37;
    io_config.dc_gpio_num = GPIO_NUM_34;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40000000;
    io_config.trans_queue_depth = 4;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io),
                        "cardputer_ui", "create LCD IO");

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_33;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_config, &s_panel),
                        "cardputer_ui", "create ST7789");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), "cardputer_ui", "reset LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), "cardputer_ui", "initialize LCD");
    // The ST7789's native offsets are (52, 40) in portrait.  After swap_xy,
    // the 240x135 Cardputer landscape window starts at (40, 53).
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 40, 53), "cardputer_ui", "set LCD gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), "cardputer_ui", "rotate LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), "cardputer_ui", "mirror LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), "cardputer_ui", "invert LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), "cardputer_ui", "enable LCD");
    gpio_set_direction(GPIO_NUM_38, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_38, 1);

    s_framebuffer = static_cast<uint16_t *>(malloc(LCD_WIDTH * LCD_HEIGHT * 2));
    ESP_RETURN_ON_FALSE(s_framebuffer != nullptr, ESP_ERR_NO_MEM, "cardputer_ui",
                        "allocate framebuffer");
    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex != nullptr, ESP_ERR_NO_MEM, "cardputer_ui",
                        "create UI mutex");
    memset(&s_state, 0, sizeof(s_state));
    s_dirty = true;
    ESP_RETURN_ON_FALSE(xTaskCreate(display_task, "cardputer_ui", 4096, nullptr, 4, nullptr) == pdPASS,
                        ESP_ERR_NO_MEM, "cardputer_ui", "create UI task");
    return ESP_OK;
}

extern "C" void cardputer_ui_update(const cardputer_ui_state_t *state)
{
    if (state == nullptr || s_mutex == nullptr) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (memcmp(&s_state, state, sizeof(s_state)) != 0) {
        s_state = *state;
        s_dirty = true;
    }
    xSemaphoreGive(s_mutex);
}
