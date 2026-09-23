#include "trf_core/trf_file.h"

#include <stdio.h>
#include <stdlib.h>

trf_result trf_file_read_all(const char* path, uint8_t** data_out, size_t* size_out)
{
    FILE* file;
    long length;
    uint8_t* buffer;
    size_t read;

    if (!path || !data_out || !size_out) return TRF_ERR_ARGUMENT;
    *data_out = NULL;
    *size_out = 0;

    file = fopen(path, "rb");
    if (!file) return TRF_ERR_IO;

    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return TRF_ERR_IO; }
    length = ftell(file);
    if (length < 0) { fclose(file); return TRF_ERR_IO; }
    if (fseek(file, 0, SEEK_SET) != 0) { fclose(file); return TRF_ERR_IO; }

    buffer = (uint8_t*)malloc((size_t)length ? (size_t)length : 1);
    if (!buffer) { fclose(file); return TRF_ERR_MEMORY; }

    read = fread(buffer, 1, (size_t)length, file);
    fclose(file);

    if (read != (size_t)length) { free(buffer); return TRF_ERR_IO; }

    *data_out = buffer;
    *size_out = (size_t)length;
    return TRF_OK;
}

trf_result trf_file_write_all(const char* path, const void* data, size_t size)
{
    FILE* file;
    size_t written;

    if (!path) return TRF_ERR_ARGUMENT;

    file = fopen(path, "wb");
    if (!file) return TRF_ERR_IO;

    written = size ? fwrite(data, 1, size, file) : 0;
    if (fclose(file) != 0) return TRF_ERR_IO;

    return written == size ? TRF_OK : TRF_ERR_IO;
}

trf_bool trf_file_exists(const char* path)
{
    FILE* file = path ? fopen(path, "rb") : NULL;
    if (!file) return TRF_FALSE;
    fclose(file);
    return TRF_TRUE;
}

int64_t trf_file_size(const char* path)
{
    FILE* file = path ? fopen(path, "rb") : NULL;
    long length;
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    length = ftell(file);
    fclose(file);
    return length;
}
