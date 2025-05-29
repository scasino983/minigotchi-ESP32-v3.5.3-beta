#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include <functional>
#include <string>
#include <map>

class TaskManager {
public:
    static TaskManager& getInstance();
    TaskManager(TaskManager const&) = delete;
    void operator=(TaskManager const&) = delete;
    bool createTask(const std::string& name, TaskFunction_t function, uint32_t stackSize = 4096, UBaseType_t priority = 1, void* parameters = nullptr, BaseType_t core = -1);
    bool suspendTask(const std::string& name);
    bool resumeTask(const std::string& name);
    bool deleteTask(const std::string& name, uint32_t timeout_ms = 2000);
    bool isTaskRunning(const std::string& name);
    TaskHandle_t getTaskHandle(const std::string& name);
    void printTaskStats();
private:
    TaskManager();
    ~TaskManager();
    static SemaphoreHandle_t task_mutex;
    std::map<std::string, TaskHandle_t> tasks;
    bool signalTaskToExit(const std::string& name, uint32_t timeout_ms);
};

extern "C" bool taskShouldExit(const char* task_name);

#endif // TASK_MANAGER_H
