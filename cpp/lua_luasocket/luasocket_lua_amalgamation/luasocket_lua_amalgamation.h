//
//  luasocket_lua_amalgamation.h
//  Pods
//
//  Created by Switt Kongdachalert on 19/2/2565 BE.
//

#ifndef luasocket_lua_amalgamation_h
#define luasocket_lua_amalgamation_h

#include "lua.h"

extern const char *luasocket_ftp_luafilesource;
extern const char *luasocket_headers_luafilesource;
extern const char *luasocket_http_luafilesource;
extern const char *luasocket_ltn12_luafilesource;
extern const char *luasocket_mbox_luafilesource;
extern const char *luasocket_mime_luafilesource;
extern const char *luasocket_smtp_luafilesource;
extern const char *luasocket_socket_luafilesource;
extern const char *luasocket_tp_luafilesource;
extern const char *luasocket_url_luafilesource;

void luasocket_preload_luasrc_definitions(lua_State *L);
//smtp
//socket
//tp
//url
#endif /* luasocket_lua_amalgamation_h */
