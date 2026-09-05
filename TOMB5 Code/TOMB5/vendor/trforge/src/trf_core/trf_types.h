/* TRForge - shared scalar types.
 *
 * Deliberately plain C so that the low level pieces (byte streams, zlib,
 * JSON) can be reused from both the C++ level library and any C tooling.
 */
#ifndef TRF_TYPES_H
#define TRF_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t trf_bool;
#define TRF_TRUE  1
#define TRF_FALSE 0

/* Result codes returned by the C layer. */
typedef enum trf_result
{
    TRF_OK = 0,
    TRF_ERR_EOF = -1,          /* read past the end of the buffer      */
    TRF_ERR_RANGE = -2,        /* seek outside the buffer              */
    TRF_ERR_MEMORY = -3,       /* allocation failed                    */
    TRF_ERR_IO = -4,           /* file could not be opened/read/written*/
    TRF_ERR_COMPRESS = -5,     /* zlib failure                         */
    TRF_ERR_FORMAT = -6,       /* malformed input                      */
    TRF_ERR_ARGUMENT = -7      /* caller passed nonsense               */
} trf_result;

const char* trf_result_text(trf_result result);

/* One click is a quarter of a sector; a sector is 1024 world units. */
#define TRF_SECTOR      1024
#define TRF_CLICK        256
#define TRF_NO_ROOM      255
#define TRF_NO_BOX     0xFFFF

#ifdef __cplusplus
}
#endif

#endif /* TRF_TYPES_H */
