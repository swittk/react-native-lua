#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LuaSocket calls these while an interpreter is executing. Waits are sliced
 * so cancellation is observable, and are capped by the active Lua deadline.
 */
double rnlua_socket_wait_timeout(double requested_seconds);
void rnlua_socket_check_interrupt(void);

#ifdef __cplusplus
}
#endif
