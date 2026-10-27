#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

typedef struct {
    FILE *file;
    pthread_mutex_t mutex;
    double start_time;
} logger_t;

int logger_open(logger_t *logger, const char *path, double start_time);
void logger_event(logger_t *logger, uint16_t local_port, const char *event,
                  const char *remote_ip, uint16_t remote_port, int piece_index,
                  uint32_t bytes, const char *status, const char *details);
void logger_close(logger_t *logger);

#endif
