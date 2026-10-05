// Compiler-generated struct copies and zeroing need these primitives even in
// freestanding builds. Keep them local: the Android mod links no libc.
extern "C" __attribute__((visibility("hidden")))
void* memset(void* target, int value, unsigned long size) {
    auto bytes = static_cast<volatile unsigned char*>(target);
    for (unsigned long i = 0; i < size; ++i) bytes[i] = static_cast<unsigned char>(value);
    return target;
}

extern "C" __attribute__((visibility("hidden")))
void* memcpy(void* target, const void* source, unsigned long size) {
    auto out = static_cast<volatile unsigned char*>(target);
    auto in = static_cast<const volatile unsigned char*>(source);
    for (unsigned long i = 0; i < size; ++i) out[i] = in[i];
    return target;
}

