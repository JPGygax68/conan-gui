#pragma once


void imgui_init(const char *window_title);

bool imgui_continue();

void imgui_new_frame();
void imgui_frame_done();

void imgui_cleanup();

auto imgui_default_font_size() -> float;
