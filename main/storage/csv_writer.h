#ifndef CSV_WRITER_H
#define CSV_WRITER_H

#include <stdio.h>

#include "esp_err.h"

#include "displacement_sample.h"

typedef struct {
    FILE *file;
} csv_writer_t;

esp_err_t csv_writer_open(csv_writer_t *writer, const char *path);
void csv_writer_write_sample(csv_writer_t *writer, const displacement_sample_t *sample);
void csv_writer_flush(csv_writer_t *writer);
void csv_writer_close(csv_writer_t *writer);

#endif
