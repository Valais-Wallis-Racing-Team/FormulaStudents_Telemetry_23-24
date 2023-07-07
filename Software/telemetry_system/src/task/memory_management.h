#ifndef __MEMORY_MANAGEMENT_H
#define __MEMORY_MANAGEMENT_H

#include <zephyr/kernel.h>

#define MAX_SERVERS 5
#define MAX_SENSORS 100

//memory heap
extern struct k_heap messageHeap;
extern struct k_heap sensorHeap;

//queues
extern struct k_queue udpQueue;
extern int udpQueueMesLength;

//sensor buffer
typedef struct sSensor{
    char* name_wifi;
    char* name_log;
    uint32_t value;
    bool wifi_enable;
    bool log_enable;
}tSensor;

extern tSensor sensorBuffer[MAX_SENSORS];


#endif /*__MEMORY_MANAGEMENT_H*/