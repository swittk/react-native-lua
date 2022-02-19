//
//  skrnlua_multithread_define.h
//  Pods
//
//  Created by Switt Kongdachalert on 17/2/2565 BE.
//

#ifndef skrnlua_multithread_define_h
#define skrnlua_multithread_define_h

struct lua_State;

extern void SKRNLuaMultitheadUserStateOpen(struct lua_State *L);
extern void SKRNLuaMultitheadUserStateClose(struct lua_State *L);
extern void SKRNLuaMultitheadLuaLock(struct lua_State *L);
extern void SKRNLuaMultitheadLuaUnlock(struct lua_State *L);
extern const char *SKRNLuaGetLuaDefaultLibraryPATH(void);

#define luai_userstateopen(L) SKRNLuaMultitheadUserStateOpen(L)
#define luai_userstateclose(L) SKRNLuaMultitheadUserStateClose(L)
#define lua_lock(L) SKRNLuaMultitheadLuaLock(L);
#define lua_unlock(L) SKRNLuaMultitheadLuaUnlock(L);

#define LUA_PATH_DEFAULT SKRNLuaGetLuaDefaultLibraryPATH()

#endif /* skrnlua_multithread_define_h */
