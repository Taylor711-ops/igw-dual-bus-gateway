#ifndef IGW3_FILE_ENGINE_H
#define IGW3_FILE_ENGINE_H
#include <stddef.h>
#include <stdint.h>

/* Call the engine from one thread only.  The board and the native fixture
 * provide this monotonic clock; no hardware registers are touched here. */
uint64_t rs3_clock_ms(void);
int rs3_file_init(void);
void rs3_file_tick(void);
size_t rs3_file_request(const uint8_t *frame, size_t len, uint8_t out[256]);
const char *rs3_storage_reason(void);
unsigned rs3_storage_error(void);
#endif
