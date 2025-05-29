#include "task_manager.h"
#include "mood.h"
#include <Arduino.h>

SemaphoreHandle_t TaskManager::task_mutex = NULL;
static std::map<std::string, bool> task_exit_flags;

TaskManager::TaskManager() {
    task_mutex = xSemaphoreCreateMutex();
    if (task_mutex == NULL) {
        Serial.println("TaskManager: Failed to create mutex!");
    }
}

TaskManager::~TaskManager() {
    if (task_mutex != NULL) {
        vSemaphoreDelete(task_mutex);
    }
}

TaskManager& TaskManager::getInstance() {
    static TaskManager instance;
    return instance;
}

bool TaskManager::createTask(const std::string& name, TaskFunction_t function, uint32_t stackSize, UBaseType_t priority, void* parameters, BaseType_t core) {
    if (xSemaphoreTake(task_mutex, portMAX_DELAY) != pdTRUE) {
        Serial.println("TaskManager: Failed to acquire mutex for task creation!");
        return false;
    }
    if (tasks.find(name) != tasks.end() && tasks[name] != NULL) {
        Serial.printf("TaskManager: Task %s already exists! Deleting first.\n", name.c_str());
        deleteTask(name);
    }
    task_exit_flags[name] = false;
    TaskHandle_t handle = NULL;
    BaseType_t result;
    if (core >= 0) {
        result = xTaskCreatePinnedToCore(function, name.c_str(), stackSize, parameters, priority, &handle, core);
    } else {
        result = xTaskCreate(function, name.c_str(), stackSize, parameters, priority, &handle);
    }
    if (result == pdPASS && handle != NULL) {
        tasks[name] = handle;
        Serial.printf("TaskManager: Created task '%s' successfully\n", name.c_str());
        xSemaphoreGive(task_mutex);
        return true;
    } else {
        Serial.printf("TaskManager: Failed to create task '%s'\n", name.c_str());
        xSemaphoreGive(task_mutex);
        return false;
    }
}

bool TaskManager::suspendTask(const std::string& name) {
    if (xSemaphoreTake(task_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    auto it = tasks.find(name);
    if (it != tasks.end() && it->second != NULL) {
        vTaskSuspend(it->second);
        Serial.printf("TaskManager: Suspended task '%s'\n", name.c_str());
        xSemaphoreGive(task_mutex);
        return true;
    }
    xSemaphoreGive(task_mutex);
    return false;
}

bool TaskManager::resumeTask(const std::string& name) {
    if (xSemaphoreTake(task_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    auto it = tasks.find(name);
    if (it != tasks.end() && it->second != NULL) {
        vTaskResume(it->second);
        Serial.printf("TaskManager: Resumed task '%s'\n", name.c_str());
        xSemaphoreGive(task_mutex);
        return true;
    }
    xSemaphoreGive(task_mutex);
    return false;
}

bool TaskManager::signalTaskToExit(const std::string& name, uint32_t timeout_ms) {
    task_exit_flags[name] = true;
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t max_wait_ticks = pdMS_TO_TICKS(timeout_ms);
    while (isTaskRunning(name) && (xTaskGetTickCount() - start_time) < max_wait_ticks) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return !isTaskRunning(name);
}

bool TaskManager::deleteTask(const std::string& name, uint32_t timeout_ms) {
    if (xSemaphoreTake(task_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    auto it = tasks.find(name);
    if (it != tasks.end() && it->second != NULL) {
        bool graceful_exit = signalTaskToExit(name, timeout_ms);
        if (!graceful_exit) {
            Serial.printf("TaskManager: Forcing deletion of task '%s'\n", name.c_str());
            vTaskDelete(it->second);
        } else {
            Serial.printf("TaskManager: Task '%s' exited gracefully\n", name.c_str());
        }
        tasks.erase(it);
        xSemaphoreGive(task_mutex);
        return true;
    }
    xSemaphoreGive(task_mutex);
    return false;
}

bool TaskManager::isTaskRunning(const std::string& name) {
    if (xSemaphoreTake(task_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    auto it = tasks.find(name);
    if (it != tasks.end() && it->second != NULL) {
        eTaskState state = eTaskGetState(it->second);
        bool running = (state != eDeleted && state != eInvalid);
        xSemaphoreGive(task_mutex);
        return running;
    }
    xSemaphoreGive(task_mutex);
    return false;
}

TaskHandle_t TaskManager::getTaskHandle(const std::string& name) {
    if (xSemaphoreTake(task_mutex, portMAX_DELAY) != pdTRUE) {
        return NULL;
    }
    auto it = tasks.find(name);
    if (it != tasks.end()) {
        TaskHandle_t handle = it->second;
        xSemaphoreGive(task_mutex);
        return handle;
    }
    xSemaphoreGive(task_mutex);
    return NULL;
}

void TaskManager::printTaskStats() {
    if (xSemaphoreTake(task_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    Serial.println("\n--- Task Manager Statistics ---");
    Serial.printf("Number of managed tasks: %d\n", tasks.size());
    for (const auto& pair : tasks) {
        const std::string& task_name = pair.first;
        TaskHandle_t handle = pair.second;
        if (handle != NULL) {
            eTaskState state = eTaskGetState(handle);
            UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(handle);
            Serial.printf("Task: %-20s | State: ", task_name.c_str());
            switch (state) {
                case eRunning: Serial.print("Running  "); break;
                case eReady: Serial.print("Ready    "); break;
                case eBlocked: Serial.print("Blocked  "); break;
                case eSuspended: Serial.print("Suspended"); break;
                case eDeleted: Serial.print("Deleted  "); break;
                default: Serial.print("Unknown  "); break;
            }
            Serial.printf(" | Stack HWM: %u bytes\n", stack_high_water);
        } else {
            Serial.printf("Task: %-20s | State: Invalid handle\n", task_name.c_str());
        }
    }
    Serial.println("-------------------------------");
    xSemaphoreGive(task_mutex);
}

extern "C" bool taskShouldExit(const char* task_name) {
    std::string name(task_name);
    auto it = task_exit_flags.find(name);
    if (it != task_exit_flags.end()) {
        return it->second;
    }
    return false;
}
