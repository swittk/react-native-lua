#pragma once

#ifndef _WIN32
#include <netdb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LuaSocket calls these while an interpreter is executing. Waits are sliced
 * so cancellation is observable, and are capped by the active Lua deadline.
 */
double rnlua_socket_wait_timeout(double requested_seconds);
void rnlua_socket_check_interrupt(void);

#ifndef _WIN32
/*
 * Resolver calls are isolated onto helper threads that own copied resolver
 * input/state. The Lua execution thread polls cancellation/deadline while the
 * platform resolver is blocked; an abandoned helper never touches LuaRuntime.
 */
int rnlua_socket_getaddrinfo(
    const char *node,
    const char *service,
    const struct addrinfo *hints,
    struct addrinfo **result);
#endif

#ifdef __cplusplus
}
#endif
