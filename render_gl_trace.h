#pragma once

// Game GL imports and supported instanced/indirect EGL proc-address calls.
enum RenderGlOperation { RenderGlDrawArrays, RenderGlDrawElements,
    RenderGlBufferData, RenderGlBufferSubData, RenderGlDrawArraysInstanced,
    RenderGlDrawElementsInstanced, RenderGlDrawArraysIndirect, RenderGlDrawElementsIndirect,
    RenderGlOperationCount };
struct RenderGlCounter {
    unsigned long long calls, units, samples, sampleNs, sampleMaxNs;
};
struct RenderGlSnapshot { RenderGlCounter operations[RenderGlOperationCount]; };
void render_gl_trace_configure(long long (*clock)());
bool render_gl_trace_install(unsigned long gameBase);
void render_gl_trace_disable();
// Approximate concurrent boundary. Units: vertices/indices including instances,
// or requested bytes. Indirect command counts stay unknown (units = 0).
RenderGlSnapshot render_gl_trace_snapshot();

// Called on the native drawing thread; true means the observer supplied the draw.
using RenderGlDrawObserver = bool (*)();
bool render_gl_trace_set_draw_observer(RenderGlDrawObserver observer);
