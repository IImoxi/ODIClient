#pragma once

struct GpuMultidrawSnapshot {
    unsigned long long array_batches, element_batches, commands, avoided_calls, fallback_batches;
};
void gpu_multidraw_init();
void gpu_multidraw_frame();
bool gpu_multidraw_install(unsigned long gameBase);
const char* gpu_multidraw_status();
bool gpu_multidraw_available();
GpuMultidrawSnapshot gpu_multidraw_snapshot();
bool client_gpu_multidraw_enabled();
void client_set_gpu_multidraw(bool enabled);
