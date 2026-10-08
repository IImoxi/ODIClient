#pragma once
void environment_init();
const char* environment_error();
float environment_sky_angle();

// Copied local weather override; safe on any thread, no native objects exposed.
struct EnvironmentWeather { bool enabled; float rain, thunder; };
EnvironmentWeather environment_weather();
