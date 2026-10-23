#ifndef PRAKTOR_SERVICE_PROCESS_HOST_H
#define PRAKTOR_SERVICE_PROCESS_HOST_H
#include "service_control.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct praktor_service_process_host { void *impl; } praktor_service_process_host;
/* Explicit synchronous C binding. Each command is owned by the joined C Process
 * worker. No shell flags, C++ runtime or fallback. Metadata and capture limits
 * are fixed at host init. Returned output is borrowed until the next run or
 * close. Single driver; the host must outlive its bound callbacks. */
int praktor_service_process_host_init(praktor_service_process_host *, const praktor_service_config *);
int praktor_service_process_host_bind(praktor_service_process_host *, praktor_service_host *);
/* Join native cleanup before releasing storage. Errors retain ownership. */
int praktor_service_process_host_close(praktor_service_process_host *);
#ifdef __cplusplus
}
#endif
#endif
