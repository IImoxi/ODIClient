#pragma once
using GpuProgramInvalidated = void (*)(unsigned int program);
// A null result forwards native source. Replacement storage must outlive the driver call.
using GpuShaderSourceTransform = const char* (*)(int count, const char* const* sources,
                                                  const int* lengths, int* replacementLength);
using GpuShaderSourceMatched = void (*)(unsigned int shader, const char* replacement);
using GpuShaderCompileObserver = void (*)(unsigned int shader);
void gpu_shader_services_init();
bool gpu_shader_services_install(unsigned long gameBase);
bool gpu_shader_services_set_shader_source_transform(GpuShaderSourceTransform transform);
bool gpu_shader_services_set_shader_diagnostics(GpuShaderSourceMatched matched,
                                              GpuShaderCompileObserver compiled);
using GpuProgramBound = void (*)(unsigned int program);
bool gpu_shader_services_set_program_observer(GpuProgramBound bound, GpuProgramInvalidated invalidated);
