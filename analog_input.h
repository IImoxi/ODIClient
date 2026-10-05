#pragma once
void analog_input_preinit();
void analog_input_update(bool enabled, bool gameplay, bool focused, bool autoSprint);
bool analog_input_connected();
bool analog_input_sensor_ready();
bool analog_input_supported();
