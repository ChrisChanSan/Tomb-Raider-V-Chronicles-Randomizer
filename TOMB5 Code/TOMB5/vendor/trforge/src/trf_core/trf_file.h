/* TRForge - whole file helpers. Levels are read into memory in one go; every
 * parser works over a span rather than a stream so that back-references and
 * the offset tables in TR5 rooms stay cheap. */
#ifndef TRF_FILE_H
#define TRF_FILE_H

#include "trf_core/trf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Caller frees *data_out with free(). */
trf_result trf_file_read_all(const char* path, uint8_t** data_out, size_t* size_out);
trf_result trf_file_write_all(const char* path, const void* data, size_t size);
trf_bool   trf_file_exists(const char* path);
int64_t    trf_file_size(const char* path);

#ifdef __cplusplus
}
#endif

#endif /* TRF_FILE_H */
