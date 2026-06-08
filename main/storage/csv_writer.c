#include "csv_writer.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "csv";

static void fprint_q8(FILE *f, int32_t q8)
{
    int64_t v = q8;
    if (v < 0) {
        fputc('-', f);
        v = -v;
    }

    int64_t whole = v >> 8;
    int64_t frac = ((v & 0xff) * 1000 + 128) / 256;
    if (frac >= 1000) {
        whole++;
        frac -= 1000;
    }
    fprintf(f, "%" PRId64 ".%03" PRId64, whole, frac);
}

static void write_header(FILE *f)
{
    fprintf(f, "frame_index,t_us,dt_us,capture_wait_us,process_us,batch_wait_us,dropped_frames,valid_mask");
    for (int i = 0; i < APP_TARGET_COUNT; i++) {
        fprintf(f,
                ",t%d_valid,t%d_cx_px,t%d_cy_px,t%d_dx_px,t%d_dy_px"
                ",t%d_threshold,t%d_pixels,t%d_quality",
                i + 1, i + 1, i + 1, i + 1, i + 1,
                i + 1, i + 1, i + 1);
    }
    fputc('\n', f);
}

esp_err_t csv_writer_open(csv_writer_t *writer, const char *path)
{
    memset(writer, 0, sizeof(*writer));
    writer->file = fopen(path, "w");
    if (!writer->file) {
        ESP_LOGW(TAG, "open failed: %s errno=%d (%s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }
    write_header(writer->file);
    ESP_LOGI(TAG, "opened: %s", path);
    return ESP_OK;
}

void csv_writer_write_sample(csv_writer_t *writer, const displacement_sample_t *sample)
{
    if (!writer || !writer->file || !sample) {
        return;
    }

    FILE *f = writer->file;
    fprintf(f, "%" PRIu32 ",%" PRId64 ",%" PRId64 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%u",
            sample->frame_index, sample->t_us, sample->dt_us,
            sample->capture_wait_us, sample->process_us,
            sample->batch_wait_us, sample->dropped_frames,
            (unsigned)sample->valid_mask);

    for (int i = 0; i < APP_TARGET_COUNT; i++) {
        const target_sample_t *t = &sample->target[i];
        fprintf(f, ",%u,", t->valid ? 1U : 0U);
        fprint_q8(f, t->cx_q8);
        fputc(',', f);
        fprint_q8(f, t->cy_q8);
        fputc(',', f);
        fprint_q8(f, t->dx_q8);
        fputc(',', f);
        fprint_q8(f, t->dy_q8);
        fprintf(f, ",%u,%" PRIu32 ",%u",
                (unsigned)t->threshold, t->pixel_count, (unsigned)t->quality);
    }
    fputc('\n', f);
}

void csv_writer_flush(csv_writer_t *writer)
{
    if (writer && writer->file) {
        fflush(writer->file);
    }
}

void csv_writer_close(csv_writer_t *writer)
{
    if (writer && writer->file) {
        fflush(writer->file);
        fclose(writer->file);
        writer->file = NULL;
    }
}
