/*
 * Hardware-in-the-loop test: dual TWAI loopback on ESP32-DualCAN.
 *
 * Standalone Arduino test — does not share code with the production
 * firmware. Transmits on CAN0, receives on CAN1 (and vice-versa) at
 * 500 kbit/s.
 *
 * Wiring required:
 *   CAN0_H <-> CAN1_H
 *   CAN0_L <-> CAN1_L
 *   One 120R across H/L (if neither transceiver has its onboard
 *   termination populated).
 *
 * Run with:  pio test -e hw-arduino -f test_hw_loopback
 */

#include <Arduino.h>
#include <unity.h>
#include "driver/twai.h"

static constexpr gpio_num_t PIN_CAN0_TX  = GPIO_NUM_2;
static constexpr gpio_num_t PIN_CAN0_RX  = GPIO_NUM_1;
static constexpr gpio_num_t PIN_CAN0_STB = GPIO_NUM_19;

static constexpr gpio_num_t PIN_CAN1_TX  = GPIO_NUM_4;
static constexpr gpio_num_t PIN_CAN1_RX  = GPIO_NUM_3;
static constexpr gpio_num_t PIN_CAN1_STB = GPIO_NUM_18;

static twai_handle_t bus0 = nullptr;
static twai_handle_t bus1 = nullptr;

static esp_err_t install_bus(int id, gpio_num_t tx, gpio_num_t rx,
                             twai_handle_t *out) {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        id, tx, rx, TWAI_MODE_NORMAL);
    g.tx_queue_len = 8;
    g.rx_queue_len = 16;
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t err = twai_driver_install_v2(&g, &t, &f, out);
    if (err != ESP_OK) return err;
    return twai_start_v2(*out);
}

static void drain_rx(twai_handle_t h) {
    twai_message_t msg;
    while (twai_receive_v2(h, &msg, 0) == ESP_OK) { /* discard */ }
}

void setUp(void) {
    if (bus0) drain_rx(bus0);
    if (bus1) drain_rx(bus1);
}

void tearDown(void) {}

void test_install_can0(void) {
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK,
        install_bus(0, PIN_CAN0_TX, PIN_CAN0_RX, &bus0),
        "CAN0 driver install failed");
}

void test_install_can1(void) {
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK,
        install_bus(1, PIN_CAN1_TX, PIN_CAN1_RX, &bus1),
        "CAN1 driver install failed");
}

void test_both_running(void) {
    twai_status_info_t s0 = {}, s1 = {};
    TEST_ASSERT_EQUAL(ESP_OK, twai_get_status_info_v2(bus0, &s0));
    TEST_ASSERT_EQUAL(ESP_OK, twai_get_status_info_v2(bus1, &s1));
    TEST_ASSERT_EQUAL(TWAI_STATE_RUNNING, s0.state);
    TEST_ASSERT_EQUAL(TWAI_STATE_RUNNING, s1.state);
}

void test_loopback_can0_to_can1(void) {
    twai_message_t tx = {};
    tx.identifier = 0x123;
    tx.data_length_code = 8;
    for (int i = 0; i < 8; i++) tx.data[i] = 0xA0 + i;

    TEST_ASSERT_EQUAL(ESP_OK,
        twai_transmit_v2(bus0, &tx, pdMS_TO_TICKS(200)));

    twai_message_t rx = {};
    TEST_ASSERT_EQUAL(ESP_OK,
        twai_receive_v2(bus1, &rx, pdMS_TO_TICKS(500)));

    TEST_ASSERT_EQUAL_HEX32(0x123, rx.identifier);
    TEST_ASSERT_EQUAL(8, rx.data_length_code);
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA0 + i, rx.data[i]);
    }
}

void test_loopback_can1_to_can0(void) {
    twai_message_t tx = {};
    tx.identifier = 0x456;
    tx.data_length_code = 4;
    tx.data[0] = 0xDE;
    tx.data[1] = 0xAD;
    tx.data[2] = 0xBE;
    tx.data[3] = 0xEF;

    TEST_ASSERT_EQUAL(ESP_OK,
        twai_transmit_v2(bus1, &tx, pdMS_TO_TICKS(200)));

    twai_message_t rx = {};
    TEST_ASSERT_EQUAL(ESP_OK,
        twai_receive_v2(bus0, &rx, pdMS_TO_TICKS(500)));

    TEST_ASSERT_EQUAL_HEX32(0x456, rx.identifier);
    TEST_ASSERT_EQUAL(4, rx.data_length_code);
    TEST_ASSERT_EQUAL_HEX8(0xDE, rx.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, rx.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, rx.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, rx.data[3]);
}

void test_burst_100_frames(void) {
    for (int i = 0; i < 100; i++) {
        twai_message_t tx = {};
        tx.identifier = 0x200 + i;
        tx.data_length_code = 2;
        tx.data[0] = i;
        tx.data[1] = i >> 8;
        TEST_ASSERT_EQUAL(ESP_OK,
            twai_transmit_v2(bus0, &tx, pdMS_TO_TICKS(100)));

        twai_message_t rx = {};
        TEST_ASSERT_EQUAL(ESP_OK,
            twai_receive_v2(bus1, &rx, pdMS_TO_TICKS(100)));
        TEST_ASSERT_EQUAL_HEX32(0x200 + i, rx.identifier);
        TEST_ASSERT_EQUAL_HEX8(i & 0xFF, rx.data[0]);
        TEST_ASSERT_EQUAL_HEX8((i >> 8) & 0xFF, rx.data[1]);
    }
}

void test_error_counters_clean(void) {
    twai_status_info_t s0 = {}, s1 = {};
    twai_get_status_info_v2(bus0, &s0);
    twai_get_status_info_v2(bus1, &s1);
    TEST_ASSERT_EQUAL(0, s0.tx_error_counter);
    TEST_ASSERT_EQUAL(0, s0.rx_error_counter);
    TEST_ASSERT_EQUAL(0, s0.bus_error_count);
    TEST_ASSERT_EQUAL(0, s1.tx_error_counter);
    TEST_ASSERT_EQUAL(0, s1.rx_error_counter);
    TEST_ASSERT_EQUAL(0, s1.bus_error_count);
}

void setup() {
    delay(2000); // wait for USB CDC host enumeration

    pinMode(PIN_CAN0_STB, OUTPUT);
    pinMode(PIN_CAN1_STB, OUTPUT);
    digitalWrite(PIN_CAN0_STB, LOW);
    digitalWrite(PIN_CAN1_STB, LOW);

    UNITY_BEGIN();
    RUN_TEST(test_install_can0);
    RUN_TEST(test_install_can1);
    RUN_TEST(test_both_running);
    RUN_TEST(test_loopback_can0_to_can1);
    RUN_TEST(test_loopback_can1_to_can0);
    RUN_TEST(test_burst_100_frames);
    RUN_TEST(test_error_counters_clean);
    UNITY_END();
}

void loop() {}
