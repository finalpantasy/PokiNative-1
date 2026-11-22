// KarPur: the native parts of Pokemon Compass's exefs plugin (subsdk9, "compass_exlaunch"), patch set compass
// (tools/fps_patches.py; docs/compass-exefs.md). The plugin is Switch code that hooks the game's code at run time; it
// cannot run here, so these host functions do what its hooks do, at the same places:
//
//   +0xf00 main+0x1107af4 (bl main+0x3b420): the Lua state that call returns is the one the hooks below use (the
//          plugin's H1; the last one wins). Until it has one, the newest state lua_mod.c saw run its main script.
//   +0xf10 main+0x1781b80 (bl main+0x1781ce8, the EXP gain, H3): byte +0x1d of the gain's object is cleared, the game
//          computes the EXP, then Compass's Lua sets the level cap and EXP rate (compass.prototype.Update_Config(0) and
//          (1)) and the ceiling (compass.prototype.Level_Cap_Check(cap)): no EXP at or above the cap, else
//          min(EXP x rate, ceiling).
//   +0xf20 main+0x114e990 (in main+0x114e98c, the party order change, H6): compass.update_battle_order(from, to, d);
//          every register is put back, the game's code goes on unchanged.
//   +0xf30 main+0x1141b9c (bl main+0xe7e274, H7): the boolean that call returns is overridden as the plugin's Lua
//          functions S1C95BFA15FD0AD5D (0) and SDE64977E8D1E8DB4 (c1A1CBE3B.fFCE06E04():f62782BA1()) ask, through
//          "KARPUR_CMD compass h7 zero|query" (the mod pack's prelude.lua prints it; lua_mod.c hands it here).
//   main+0x183b3e4 (H4) is a word patch in the table: the food power time 1800.0 (30 min) becomes 3600.0.
// The plugin's inline hook H5 (w21 = 1 at main+0x108370c) does nothing in 4.0.0: the instruction it runs before,
// orr w21, w8, w9, sets w21 itself. Not ported.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

#define HOST_STATE 0xf00
#define HOST_EXP 0xf10
#define HOST_ORDER 0xf20
#define HOST_W0 0xf30
#define FN_STATE 0x3b420      // main offsets in 4.0.0, checked by tools/fps_patches.py
#define FN_EXP 0x1781ce8
#define FN_W0 0xe7e274

static uint64_t base;
static uint64_t cached_state;   // H1: the newest state main+0x3b420 returned
static int level_cap = 100;     // the plugin's g_levelCap, g_expMult and g_flag, with their start values
static float exp_rate = 1.0f;
static uint8_t flag = 1;
static int w0_mode;             // 0: no override, 1: W0 = 0 (S1C95..), 2: W0 from Lua (SDE64..)
static int init_done;           // CEE4BBD1699D28F52.prototype.F5B48522432659856() ran once without error
static const float rates[16] = {0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f,
                                1.4f, 1.5f, 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f};

static uint64_t state(void) {
    uint64_t L = __atomic_load_n(&cached_state, __ATOMIC_ACQUIRE);
    return L ? L : lua_mod_last_state();
}

static void host_state(Cpu *c) {
    uint64_t a[8];
    memcpy(a, c->x, sizeof a);
    uint64_t r = rt_call(c, base + FN_STATE, 8, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    if (r) __atomic_store_n(&cached_state, r, __ATOMIC_RELEASE);
    static int n;
    if (__atomic_add_fetch(&n, 1, __ATOMIC_RELAXED) <= 8) rt_log("compass: Lua state %#llx (main+%#x)", (unsigned long long)r, FN_STATE);
    c->x[0] = r;
}

// Update_Config(flag) as the plugin does it; a failure or a non-number puts all three values back to their defaults.
static void update_config(Cpu *c, uint64_t L, int which) {
    char src[160];
    int64_t r = 0;
    snprintf(src, sizeof src, "local flag_type = %d\nlocal results = compass.prototype.Update_Config(flag_type)\nreturn results", which);
    if (!lua_mod_eval_int(c, L, src, &r)) {
        level_cap = 100, exp_rate = 1.0f;
        __atomic_store_n(&flag, 1, __ATOMIC_RELAXED);
        return;
    }
    if (which == 0) level_cap = r ? (int)r : 100;
    else if (which == 1) exp_rate = (uint64_t)(uint32_t)r <= 15 && (r >> 32) == 0 ? rates[r] : 1.0f;
}

static void host_exp(Cpu *c) {
    uint64_t obj = c->x[0], out = c->x[1];
    *(uint8_t *)(uintptr_t)(obj + 0x1d) = 0;
    uint64_t r0 = rt_call(c, base + FN_EXP, 2, obj, out);
    uint64_t L = state();
    if (!L) return;
    int64_t v = 0;
    if (!init_done && lua_mod_eval_int(c, L, "CEE4BBD1699D28F52.prototype.F5B48522432659856()\nreturn 1", &v)) init_done = 1;
    update_config(c, L, 0);
    update_config(c, L, 1);
    char src[160];
    snprintf(src, sizeof src, "local level = %d\nlocal results = compass.prototype.Level_Cap_Check(level)\nreturn results", level_cap);
    int64_t lim64 = -1;
    if (!lua_mod_eval_int(c, L, src, &lim64)) lim64 = -1;
    int32_t lim = (int32_t)lim64;
    uint32_t level = *(const uint32_t *)(uintptr_t)(obj + 0x10), before = *(const uint32_t *)(uintptr_t)out, after;
    if (level >= (uint32_t)level_cap) after = 0;
    else {
        int32_t e = (int32_t)truncf((float)before * exp_rate);
        after = (uint32_t)(e <= lim ? e : lim);
    }
    *(uint32_t *)(uintptr_t)out = after;
    static int n;
    if (__atomic_add_fetch(&n, 1, __ATOMIC_RELAXED) <= 40)
        rt_log("compass: EXP %u -> %u (level %u, level cap %d, EXP rate %d%%, ceiling %d)", before, after, level & 0xffff,
               level_cap, (int)lroundf(exp_rate * 100), lim);
    c->x[0] = r0;
}

static void host_order(Cpu *c) {
    uint32_t a = (uint32_t)c->x[1] & 0xff, from = (uint32_t)c->x[2] & 0xff, to = (uint32_t)c->x[3] & 0xff;
    int32_t d = (int32_t)c->x[4];
    uint64_t L = state();
    if (!L || a != 0 || from == to) return;
    Cpu saved;   // the game's code goes on after this call: every register as it was
    memcpy(&saved, c, sizeof saved);
    char src[200];
    snprintf(src, sizeof src, "local f = compass ~= nil and compass.update_battle_order\nif f ~= nil then f(%u, %u, %d) end\nreturn 0",
             from, to, d);
    int64_t v;
    int ok = lua_mod_eval_int(c, L, src, &v);
    memcpy(c, &saved, sizeof saved);
    static int n;
    if (__atomic_add_fetch(&n, 1, __ATOMIC_RELAXED) <= 20)
        rt_log("compass: party order %u -> %u (%d)%s", from, to, d, ok ? "" : ": update_battle_order failed");
}

static void host_w0(Cpu *c) {
    uint64_t a[8];
    memcpy(a, c->x, sizeof a);
    uint64_t r = rt_call(c, base + FN_W0, 8, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    int mode = __atomic_load_n(&w0_mode, __ATOMIC_RELAXED), f = __atomic_load_n(&flag, __ATOMIC_RELAXED) & 1;
    uint64_t was = r;
    if (mode == 1 && !f) r &= ~0xffffffffull;
    else if (mode == 2 && f) {
        int64_t q = -1;
        uint64_t L = state();
        if (!L || !lua_mod_eval_int(c, L, "local results = c1A1CBE3B.fFCE06E04():f62782BA1()\nreturn results", &q)) q = -1;
        r = (r & ~0xffffffffull) | (uint32_t)q;
    }
    static int n;
    if (mode && __atomic_add_fetch(&n, 1, __ATOMIC_RELAXED) <= 20)
        rt_log("compass: main+%#x returned %u, Compass makes it %u", FN_W0, (unsigned)(uint32_t)was, (unsigned)(uint32_t)r);
    c->x[0] = r;
}

// "h7 zero" (S1C95BFA15FD0AD5D) or "h7 query" (SDE64977E8D1E8DB4), from the mod pack's prelude.lua
void compass_command(const char *cmd, size_t len) {
    if (len == 7 && !memcmp(cmd, "h7 zero", 7)) {
        __atomic_store_n(&flag, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&w0_mode, 1, __ATOMIC_RELAXED);
    } else if (len == 8 && !memcmp(cmd, "h7 query", 8)) {
        __atomic_store_n(&flag, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&w0_mode, 2, __ATOMIC_RELAXED);
    } else {
        rt_log("compass: unknown command %.*s", (int)(len < 64 ? len : 64), cmd);
        return;
    }
    static int n;
    if (__atomic_add_fetch(&n, 1, __ATOMIC_RELAXED) <= 20) rt_log("compass: %.*s", (int)len, cmd);
}

void compass_init(void) {
    int set = rt_patch_set("compass");
    if (set < 0 || !rt_patch_page()) return;
    uint64_t page = (uint64_t)(uintptr_t)rt_patch_page();
    rt_register_host(page + HOST_STATE, host_state);
    rt_register_host(page + HOST_EXP, host_exp);
    rt_register_host(page + HOST_ORDER, host_order);
    rt_register_host(page + HOST_W0, host_w0);
    base = ldr_module(0)->base;
}
