#include "analog_input.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#include <cstddef>

static double seconds = 10;
static int clockNow(clockid_t, timespec* t) {
    t->tv_sec = static_cast<time_t>(seconds);
    t->tv_nsec = static_cast<long>((seconds - t->tv_sec) * 1000000000);
    return 0;
}
extern "C" void* mcpelauncher_host_dlopen(const char* path, int flags) {
    return dlopen(path, flags);
}
extern "C" void* mcpelauncher_host_dlsym(void* lib, const char* name) {
    if (!std::strcmp(name, "clock_gettime")) return reinterpret_cast<void*>(clockNow);
    return dlsym(lib, name);
}

int main() {
    int server = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    assert(server >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    int count = std::snprintf(address.sun_path + 1, sizeof(address.sun_path) - 1,
                              "nuphy-analog-%u", getuid());
    assert(bind(server, reinterpret_cast<sockaddr*>(&address),
                offsetof(sockaddr_un, sun_path) + 1 + count) == 0);
    assert(listen(server, 1) == 0);
    analog_input_preinit();
    assert(analog_input_supported());
    assert(!std::strcmp(std::getenv("MCPELAUNCHER_CLIENT_RAW_INPUT"), "1"));
    analog_input_update(true, true, true, false);
    int client = accept(server, nullptr, nullptr);
    assert(client >= 0);
    unsigned char packet[8];
    assert(recv(client, packet, sizeof(packet), MSG_DONTWAIT) == 8);
    const unsigned char active[] = {'N','A','C','1',1,1,0,0};
    assert(!std::memcmp(packet, active, 8));
    analog_input_update(true, true, true, false);
    assert(recv(client, packet, 8, MSG_DONTWAIT) == -1); // No per-frame duplicate traffic.
    seconds += 0.06;
    analog_input_update(true, true, true, false);
    assert(recv(client, packet, 8, MSG_DONTWAIT) == 8); // Heartbeat despite unchanged flags.
    const unsigned char status[] = {'N','A','S','1',1,0,0,0};
    assert(send(client, status, 8, 0) == 8);
    analog_input_update(true, true, false, true);
    assert(analog_input_connected() && analog_input_sensor_ready());
    assert(recv(client, packet, 8, MSG_DONTWAIT) == 8);
    assert(packet[4] == 1 && packet[5] == 0 && packet[6] == 1);
    const unsigned char stale[] = {'N','A','S','1',0,0,0,0};
    assert(send(client, stale, 8, 0) == 8);
    analog_input_update(false, true, true, false);
    assert(analog_input_connected() && !analog_input_sensor_ready());
    assert(recv(client, packet, 8, MSG_DONTWAIT) == 8 && packet[4] == 0);
    seconds += 0.6;
    analog_input_update(true, true, true, false);
    assert(!analog_input_connected());
    close(client);
    seconds += 1;
    analog_input_update(true, true, true, false);
    client = accept(server, nullptr, nullptr);
    assert(client >= 0);
    assert(recv(client, packet, 8, MSG_DONTWAIT) == 8);
    const unsigned char invalid[] = {'N','A','S','1',1,0,0,0,0};
    assert(send(client, invalid, sizeof(invalid), 0) == sizeof(invalid));
    analog_input_update(true, true, true, false);
    assert(!analog_input_connected());
    close(client);
    close(server);
    std::puts("PASS: live helper IPC, heartbeat throttling, immediate control changes, sensor status, timeout, reconnect, and oversized packet rejection");
}
