#pragma once
void flarial_presence_init();
void flarial_presence_refresh(long long now);
bool flarial_presence_matches(const char* name, long long now);
const char* flarial_presence_status();
void flarial_presence_match_many(const char* records, unsigned long stride, unsigned long nameOffset,
                                 int count, bool* matches, long long now);
