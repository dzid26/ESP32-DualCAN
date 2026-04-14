/*
 * Hardware smoke test: RGB LED on the ESP32-C6-SuperMini.
 *
 * Standalone Arduino test — does not share code with the production
 * firmware. It only proves the onboard WS2812 responds to writes.
 *
 * The ESP32-C6-SuperMini's onboard RGB LED is on GPIO 8. If your board
 * revision differs, adjust LED_PIN below.
 *
 * Run with:  pio test -e hw-arduino -f test_hw_led
 *
 * The tests always pass; verify visually by watching the LED cycle
 * red -> green -> blue -> off.
 */

#include <Arduino.h>
#include <unity.h>

static constexpr uint8_t LED_PIN = 8;
static constexpr uint8_t LED_BRIGHT = 222;   // keep it dim, 0..255

void setUp(void) {}

void tearDown(void)
{
    neopixelWrite(LED_PIN, 0, 0, 0);
}

void test_led_red(void)
{
    neopixelWrite(LED_PIN, LED_BRIGHT, 0, 0);
    TEST_PASS_MESSAGE("LED red — verify visually");
    delay(1400);
}

void test_led_green(void)
{
    neopixelWrite(LED_PIN, 0, LED_BRIGHT, 0);
    TEST_PASS_MESSAGE("LED green — verify visually");
    delay(1400);
}

void test_led_blue(void)
{
    neopixelWrite(LED_PIN, 0, 0, LED_BRIGHT);
    TEST_PASS_MESSAGE("LED blue — verify visually");
    delay(1400);
}

void setup()
{
    delay(2000);  // let USB-CDC serial come up

    UNITY_BEGIN();
    RUN_TEST(test_led_red);
    RUN_TEST(test_led_green);
    RUN_TEST(test_led_blue);
    UNITY_END();
}

void loop() {}
