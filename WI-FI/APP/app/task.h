#ifndef WI_FI_TASK_H
#define WI_FI_TASK_H

#include "OTA.h"
#include "gpio.h"

typedef enum
{
    CHECK_WIFI = 0,
    CHECK_UPDATA,
    OTA_HTTP_CLOSED
} ota_t;

void led_task(void);
void ota_task(void);

#endif //WI_FI_TASK_H