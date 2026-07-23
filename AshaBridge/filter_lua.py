Import("env") # noqa
import os 


lua_dir = os.path.join(
    env.get("PROJECT_LIBDEPS_DIR"),
    env.get("PIOENV"),
    "Esp32Lua", "src", "lua"
)

for fname in ["lua.c", "luac.c"]:
    src = os.path.join(lua_dir, fname)
    dst = src + ".disabled"
    if os.path.isfile(src):
        os.rename(src, dst)
        print(f"[filter_lua] Disabled {fname}")
