#include "logger.h"

static void csv_field(FILE *file, const char *text)
{
    fputc('"', file);
    for (const char *p = text ? text : ""; *p; p++) {
        if (*p == '"') fputc('"', file);
        fputc((*p == '\n' || *p == '\r') ? ' ' : *p, file);
    }
    fputc('"', file);
}

int logger_open(logger_t *logger, const char *path, double start_time)
{
    memset(logger, 0, sizeof(*logger));
    logger->file = fopen(path, "a");
    if (!logger->file) return -1;
    logger->start_time = start_time;
    pthread_mutex_init(&logger->mutex, NULL);
    if (ftell(logger->file) == 0) {
        fprintf(logger->file, "timestamp,elapsed_seconds,peer_port,event,remote_ip,remote_port,piece_index,bytes,status,details\n");
        fflush(logger->file);
    }
    return 0;
}

void logger_event(logger_t *logger, uint16_t local_port, const char *event,
                  const char *remote_ip, uint16_t remote_port, int piece_index,
                  uint32_t bytes, const char *status, const char *details)
{
    if (!logger || !logger->file) return;
    time_t now = time(NULL); struct tm local_time; char timestamp[32];
    localtime_r(&now, &local_time);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &local_time);
    struct timespec mono; clock_gettime(CLOCK_MONOTONIC, &mono);
    double elapsed = (double)mono.tv_sec + (double)mono.tv_nsec / 1e9 - logger->start_time;
    pthread_mutex_lock(&logger->mutex);
    csv_field(logger->file, timestamp);
    fprintf(logger->file, ",%.6f,%u,", elapsed, local_port);
    csv_field(logger->file, event); fputc(',', logger->file);
    csv_field(logger->file, remote_ip); fputc(',', logger->file);
    if (remote_port) fprintf(logger->file, "%u", remote_port);
    fputc(',', logger->file);
    if (piece_index >= 0) fprintf(logger->file, "%d", piece_index);
    fprintf(logger->file, ",%u,", bytes);
    csv_field(logger->file, status); fputc(',', logger->file);
    csv_field(logger->file, details); fputc('\n', logger->file);
    fflush(logger->file);
    pthread_mutex_unlock(&logger->mutex);
}

void logger_close(logger_t *logger)
{
    if (logger && logger->file) { fclose(logger->file); logger->file = NULL; pthread_mutex_destroy(&logger->mutex); }
}
