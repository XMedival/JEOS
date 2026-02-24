#pragma once
#include "idt.h"

#define panic(...) __panic_impl(__VA_ARGS__, 0, 0)
#define __panic_impl(msg, frame, ...) __panic(msg, frame)

__attribute__((noreturn)) void __panic(const char *msg, struct trap_frame *frame);
