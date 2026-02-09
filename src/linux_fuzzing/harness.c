#include <string.h>
#include <stdint.h>

#include "command.h" 


int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t fuzz_buf[256];
    
    if (size > 256) {
        size = 256;
    }

    memcpy(fuzz_buf, data, size);

    command_dispatch(fuzz_buf, (uint_fast8_t)size);

    return 0;
}