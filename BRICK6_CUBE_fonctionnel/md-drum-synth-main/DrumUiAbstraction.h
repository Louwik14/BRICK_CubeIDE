#pragma once

/*
 * Desktop/UI abstraction for md-drum DSP sources.
 *
 * Pass 1 drum porting rule:
 * - audio DSP code must compile without imgui/desktop dependencies.
 * - RenderControls remains available for desktop demos when explicitly enabled.
 */
#ifndef MD_DRUM_HAS_DESKTOP_UI
#define MD_DRUM_HAS_DESKTOP_UI 0
#endif
