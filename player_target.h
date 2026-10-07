#pragma once
#include "chat.h"

// Initialize once after Minecraft loads. Exact supported binary only.
void player_target_init();
const char* player_target_error();
// Call only inside a ChatLiveListener. Uses borrowed native objects synchronously,
// copies the closest loaded player under the camera ray, and retains no pointers.
// No interaction reach limit. Returns false for unsupported builds or no player.
bool player_target_name(const ChatLiveContext& context, char* name, unsigned long capacity);
