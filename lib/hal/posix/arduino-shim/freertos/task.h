#pragma once

#include "FreeRTOS.h"

using TaskHandle_t = pthread_t*;
using TaskFunction_t = void (*)(void*);

// Creates a detached pthread. The priority and core arguments are accepted and
// ignored: see the note in FreeRTOS.h about what does not carry across.
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* arg, UBaseType_t priority,
                       TaskHandle_t* created);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* arg,
                                   UBaseType_t priority, TaskHandle_t* created, BaseType_t core);
void vTaskDelete(TaskHandle_t handle);
inline void taskYIELD() { sched_yield(); }

// Task notifications: FreeRTOS's lightweight per-task counting semaphore, used
// in this tree to park a worker until there is something to do.
//
// Implemented rather than stubbed, and for the usual reason: a stub that
// returned immediately would turn a blocking wait into a busy loop, and one
// that never returned would deadlock. Both are worse than the real thing,
// which is a counter and a condition variable per task.
// How a notification combines with the value already there. Only eIncrement is
// used by this tree, and it is what ulTaskNotifyTake pairs with.
enum eNotifyAction {
  eNoAction,
  eSetBits,
  eIncrement,
  eSetValueWithOverwrite,
  eSetValueWithoutOverwrite,
};

uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t timeoutTicks);
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle();

// Alocacao estatica de tarefa. No FreeRTOS isto e o TCB que o chamador
// entrega a xTaskCreateStatic(); aqui uma tarefa e uma pthread e o kernel
// aloca o que precisa. Este tipo existe para o codigo que so faz
// sizeof(StaticTask_t) compilar. NAO e promessa de que criacao estatica
// funciona: xTaskCreateStatic continua deliberadamente ausente, entao quem
// realmente tentar criar uma tarefa estatica quebra no link, com o nome do
// sitio de chamada, e nao silenciosamente em tempo de execucao.
struct StaticTask_t {
  void* dummy[24];
};
