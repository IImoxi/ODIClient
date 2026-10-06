#pragma once

enum RenderFrameStage { FrameOutsideCallback, FrameClientCallback, FrameLimiter,
    FrameOverlays, FrameNativeSubmit, FramePresent, RenderFrameStageCount };
struct RenderFrameStamp { long long wall, cpu; };
struct RenderFrameTiming { unsigned long long calls, wallNs, wallMaxNs, cpuNs; };
struct RenderFrameSnapshot { RenderFrameTiming stages[RenderFrameStageCount]; unsigned long long gapResets; };
struct RenderFrameHooks { bool submit, present; };
void render_frame_trace_configure(long long (*wallClock)(), long long (*cpuClock)());
RenderFrameHooks render_frame_trace_install(unsigned long gameBase);
void render_frame_trace_disable();
RenderFrameStamp render_frame_trace_stamp();
RenderFrameStamp render_frame_trace_begin();
void render_frame_trace_record(RenderFrameStage stage, RenderFrameStamp start);
void render_frame_trace_end(RenderFrameStamp start);
RenderFrameSnapshot render_frame_trace_snapshot();
