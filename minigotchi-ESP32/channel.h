#ifndef CHANNEL_H
#define CHANNEL_H

#include "wifi_sniffer.h"
#include "minigotchi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

// Forward declaration of the channel hopping task
void channel_hopping_task(void *pvParameter);

// Channel hopping task handle - declared externally to be accessible from channel_hopper.cpp
extern TaskHandle_t channel_hopping_task_handle;

class Channel {
public:
  static void init(int initChannel);
  static void cycle();
  static void switchChannel(int newChannel);
  static bool checkChannel(int channel);
  static bool isValidChannel(int channel);
  static int getChannel();

private:
  static int channelList[13];
  static class Mood &mood;
};

#endif // CHANNEL_H