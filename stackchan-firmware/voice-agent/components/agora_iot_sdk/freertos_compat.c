// IDF 5.x compatibility shim for Agora's prebuilt libs (built against IDF 4.4).
// libahpl.a references xTaskCreateRestrictedPinnedToCore, which was removed in
// IDF 5 (MPU-restricted tasks). Agora only uses it as a plain pinned task
// create, so forward to xTaskCreatePinnedToCore, ignoring the MPU regions.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Layout of IDF 4.4's TaskParameters_t (first six fields; xRegions not read).
typedef struct {
    TaskFunction_t pvTaskCode;
    const char    *pcName;
    uint32_t       usStackDepth;
    void          *pvParameters;
    UBaseType_t    uxPriority;
    StackType_t   *puxStackBuffer;
} compat_task_params_t;

BaseType_t xTaskCreateRestrictedPinnedToCore(const void *pxTaskDefinition,
                                             TaskHandle_t *pxCreatedTask,
                                             const BaseType_t xCoreID)
{
    const compat_task_params_t *p = (const compat_task_params_t *)pxTaskDefinition;
    return xTaskCreatePinnedToCore(p->pvTaskCode, p->pcName, p->usStackDepth,
                                   p->pvParameters, p->uxPriority,
                                   pxCreatedTask, xCoreID);
}
