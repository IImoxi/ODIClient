#pragma once

// Imported game GL calls only: extension/proc-address paths may bypass this trace.
enum RenderGlOperation { RenderGlDrawArrays, RenderGlDrawElements,
    RenderGlBufferData, RenderGlBufferSubData, RenderGlOperationCount };
struct RenderGlCounter {
    unsigned long long calls, units, samples, sampleNs, sampleMaxNs;
};
struct RenderGlSnapshot { RenderGlCounter operations[RenderGlOperationCount]; };
void render_gl_trace_configure(long long (*clock)());
bool render_gl_trace_install(unsigned long gameBase);
void render_gl_trace_disable();
// Approximate concurrent window boundary; units are vertices/indices/requested bytes.
RenderGlSnapshot render_gl_trace_snapshot();
