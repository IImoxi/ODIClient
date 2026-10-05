#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#include <stddef.h>
#include "launcher_api.h"
#include "analog_input.h"

namespace {
decltype(&socket) openSocket;
decltype(&connect) connectSocket;
decltype(&send) sendPacket;
decltype(&recv) receivePacket;
decltype(&close) closeSocket;
decltype(&getsockopt) getOption;
decltype(&clock_gettime) getTime;
decltype(&getuid) userID;
int fd = -1;
bool supported = false, connected = false, sensorReady = false;
double retryAt, lastReply;
double lastSent;
unsigned char lastControl[8];

double now() {
    timespec t;
    getTime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}

void disconnect() {
    if (fd != -1) closeSocket(fd);
    fd = -1;
    __atomic_store_n(&connected, false, __ATOMIC_RELEASE);
    __atomic_store_n(&sensorReady, false, __ATOMIC_RELEASE);
}

void tryConnect() {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    constexpr char prefix[] = "nuphy-analog-";
    int length = 1; // Leading NUL selects an abstract, per-user Unix socket.
    for (char c : prefix) if (c) address.sun_path[length++] = c;
    unsigned int uid = userID();
    char digits[10]; int count = 0;
    do { digits[count++] = '0' + uid % 10; uid /= 10; } while (uid);
    while (count) address.sun_path[length++] = digits[--count];
    fd = openSocket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) return;
    if (connectSocket(fd, reinterpret_cast<sockaddr*>(&address), offsetof(sockaddr_un, sun_path) + length) != 0) {
        disconnect(); return;
    }
    ucred peer;
    socklen_t size = sizeof(peer);
    if (getOption(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 || peer.uid != userID()) {
        disconnect(); return;
    }
    lastReply = now();
    lastSent = 0;
}
}

void analog_input_preinit() {
    void* libc = mcpelauncher_host_dlopen("libc.so.6", 2);
    if (!libc) return;
#define LOAD(variable, name) variable = reinterpret_cast<decltype(variable)>(mcpelauncher_host_dlsym(libc, name)); if (!variable) return
    LOAD(openSocket, "socket"); LOAD(connectSocket, "connect");
    LOAD(sendPacket, "send"); LOAD(receivePacket, "recv"); LOAD(closeSocket, "close");
    LOAD(getOption, "getsockopt"); LOAD(getTime, "clock_gettime"); LOAD(userID, "getuid");
#undef LOAD
    using SetEnv = int (*)(const char*, const char*, int);
    auto setEnvironment = reinterpret_cast<SetEnv>(mcpelauncher_host_dlsym(libc, "setenv"));
    // The launcher normally selects either mouse/keyboard or controller input.
    // Set only this process's startup flag so both work together, including L.
    supported = setEnvironment && setEnvironment("MCPELAUNCHER_CLIENT_RAW_INPUT", "1", 1) == 0;
}

bool analog_input_supported() { return supported; }
bool analog_input_connected() { return __atomic_load_n(&connected, __ATOMIC_ACQUIRE); }
bool analog_input_sensor_ready() { return __atomic_load_n(&sensorReady, __ATOMIC_ACQUIRE); }

void analog_input_update(bool enabled, bool gameplay, bool focused, bool autoSprint) {
    if (!supported) return;
    double time = now();
    if (fd == -1) {
        if (time < retryAt) return;
        retryAt = time + 1;
        tryConnect();
        if (fd == -1) return;
    }
    unsigned char packet[8];
    for (int i = 0; i < 32; ++i) {
        auto size = receivePacket(fd, packet, sizeof(packet), MSG_DONTWAIT | MSG_TRUNC);
        if (size < 0) break;
        if (size != 8 || packet[0] != 'N' || packet[1] != 'A' || packet[2] != 'S'
            || packet[3] != '1' || packet[4] > 1 || packet[5] || packet[6] || packet[7]) {
            disconnect(); return;
        }
        lastReply = time;
        __atomic_store_n(&connected, true, __ATOMIC_RELEASE);
        __atomic_store_n(&sensorReady, packet[4] != 0, __ATOMIC_RELEASE);
    }
    if (time - lastReply > 0.5) { disconnect(); return; }
    unsigned char control[] = {'N', 'A', 'C', '1', static_cast<unsigned char>(enabled),
        static_cast<unsigned char>(gameplay && focused), static_cast<unsigned char>(autoSprint), 0};
    bool changed = false;
    for (int i = 0; i < 8; ++i) if (control[i] != lastControl[i]) changed = true;
    // Send state changes immediately, with a 20 Hz heartbeat rather than one per frame.
    if (!changed && lastSent && time - lastSent < 0.05) return;
    // Never block the renderer, and never raise SIGPIPE when the helper exits.
    auto sent = sendPacket(fd, control, sizeof(control), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent == sizeof(control)) {
        for (int i = 0; i < 8; ++i) lastControl[i] = control[i];
        lastSent = time;
    }
    if (sent == 0) disconnect();
}
