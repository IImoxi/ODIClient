#pragma once
struct GpuUniformCacheSnapshot { unsigned long long calls, skipped, skipped_bytes, eligible, owner_rejected, uncacheable; };
void gpu_uniform_cache_init();
void gpu_uniform_cache_frame();
void gpu_uniform_cache_trace_enable(bool enabled);
bool gpu_uniform_cache_install(unsigned long gameBase);
const char* gpu_uniform_cache_status();
bool gpu_uniform_cache_available();
GpuUniformCacheSnapshot gpu_uniform_cache_snapshot();
bool client_uniform_cache_enabled();
void client_set_uniform_cache(bool enabled);
