#include "../targets.hpp"

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    mh::scratch_init();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    mh::Enter in;
    return mh::run_sheet(data, size);
}
