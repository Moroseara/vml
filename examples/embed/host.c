/*
 * Example host program for vml.h (also run by `make embed-test`).
 * Registers a C module, loads a script, calls into it, then saves a bytecode
 * image and runs it again in a fresh state without compiling.
 */
#include "../../include/vml.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); failures++; } } while (0)

/* ---- the "game" module ---- */

typedef struct { char name[32]; int hp; } Entity;
static Entity entities[16];
static int num_entities = 0;

static VML_Result host_spawn(VML_Call *c) {
    size_t len = 0;
    const char *name = vml_arg_string(c, 0, &len);
    if (num_entities >= 16) return vml_error(c, "too many entities");
    Entity *e = &entities[num_entities];
    snprintf(e->name, sizeof(e->name), "%.*s", (int)len, name);
    e->hp = (int)vml_arg_int(c, 1);
    return vml_return_int(c, num_entities++);      /* Entity handle */
}

static VML_Result host_damage(VML_Call *c) {
    int64_t id = vml_arg_int(c, 0);
    if (id < 0 || id >= num_entities) return vml_error(c, "invalid entity");
    entities[id].hp -= (int)vml_arg_int(c, 1);
    return vml_return_int(c, entities[id].hp);
}

static VML_Result host_name(VML_Call *c) {
    int64_t id = vml_arg_int(c, 0);
    if (id < 0 || id >= num_entities) return vml_error(c, "invalid entity");
    return vml_return_string(c, entities[id].name, strlen(entities[id].name));
}

static VML_Result host_scale(VML_Call *c) {
    double *factor = (double *)vml_call_userdata(c);
    return vml_return_float(c, vml_arg_float(c, 0) * *factor);
}

static double g_scale = 2.5;

static void register_game(VML_State *L) {
    VML_Module *m = vml_module_new(L, "game");
    vml_module_set_userdata(m, &g_scale);
    vml_module_type(m, "Entity");
    vml_module_func(m, "spawn", "(name: string, hp: int): Entity", host_spawn);
    vml_module_func(m, "damage", "(e: Entity, amount: int): int", host_damage);
    vml_module_func(m, "name", "(e: Entity): string", host_name);
    vml_module_func(m, "scale", "(x: float): float", host_scale);
    vml_module_const_int(m, "MAX_PLAYERS", 16);
    vml_module_const_string(m, "TITLE", "Dungeon");
    vml_module_finish(m);
}

static const char *UTIL_SRC =
    "pub func clamp(v: int, lo: int, hi: int): int {\n"
    "    if v < lo { return lo }\n"
    "    if v > hi { return hi }\n"
    "    return v\n"
    "}\n";

static const char *SCRIPT =
    "import game\n"
    "import util\n"
    "\n"
    "func setup(): int {\n"
    "    let hero = game.spawn(\"hero\", 100)\n"
    "    let orc = game.spawn(\"orc\", 30)\n"
    "    game.damage orc, 12\n"
    "    return game.damage(hero, 7)\n"
    "}\n"
    "\n"
    "func describe(id: int): string {\n"
    "    return game.TITLE + \": \" + game.name(id as i64)\n"
    "}\n"
    "\n"
    "func clamp_hp(v: int): int { return util.clamp(v, 0, game.MAX_PLAYERS) }\n"
    "func scaled(x: float): float { return game.scale(x) }\n"
    "\n"
    "func safe_damage(id: int): int {\n"
    "    return try game.damage(id as i64, 1) or -1\n"
    "}\n"
    "\n"
    "func spin(): int {\n"
    "    let n: int = 0\n"
    "    while true { n += 1 }\n"
    "    return n\n"
    "}\n";

static void exercise(VML_State *L, const char *label) {
    printf("-- %s\n", label);
    CHECK(vml_call(L, "setup", "") == VML_OK, "setup()");
    CHECK(vml_result_int(L) == 93, "setup() == 93 (hero hp after damage)");
    CHECK(vml_call(L, "describe", "i", (int64_t)1) == VML_OK, "describe(1)");
    size_t len = 0;
    const char *s = vml_result_string(L, &len);
    CHECK(len == 12 && memcmp(s, "Dungeon: orc", 12) == 0, "describe(1) == \"Dungeon: orc\"");
    CHECK(vml_call(L, "clamp_hp", "i", (int64_t)99) == VML_OK && vml_result_int(L) == 16, "source module + host const");
    CHECK(vml_call(L, "scaled", "f", 2.0) == VML_OK && vml_result_float(L) == 5.0, "module userdata");
    CHECK(vml_call(L, "safe_damage", "i", (int64_t)999) == VML_OK && vml_result_int(L) == -1, "host error is catchable");
    VML_Result r = vml_call(L, "safe_damage", "s", "x");
    CHECK(r == VML_ERR_TYPE, "argument type checked");
    r = vml_call(L, "nope", "");
    CHECK(r == VML_ERR_NOT_FOUND, "unknown function reported");
    vml_set_fuel(L, 100000);
    r = vml_call(L, "spin", "");
    CHECK(r == VML_ERR_RUNTIME && strstr(vml_last_error(L), "fuel") != NULL, "fuel limit stops infinite loop");
    vml_set_fuel(L, 0);
}

/* ---- the "geo" module, made with the builder API ---- */

static VML_Result host_dot(VML_Call *c) {
    /* (a: Vec2, b: Vec2): fields arrive flattened: a.x, a.y, b.x, b.y */
    double r = vml_arg_float(c, 0) * vml_arg_float(c, 2) + vml_arg_float(c, 1) * vml_arg_float(c, 3);
    return vml_return_float(c, r);
}

static void register_geo(VML_State *L) {
    VML_Module *m = vml_module_new(L, "geo");

    VML_StructB *v = vml_struct_begin(m, "Vec2");
    vml_struct_field(v, "x", "float");
    vml_struct_field(v, "y", "float");
    CHECK(vml_struct_end(v) == VML_OK, "struct Vec2");

    VML_EnumB *e = vml_enum_begin(m, "Shape");
    vml_enum_variant(e, "Circle", "(r: float)");
    vml_enum_variant(e, "Rect", "(w: float, h: float)");
    vml_enum_variant(e, "Empty", NULL);
    CHECK(vml_enum_end(e) == VML_OK, "enum Shape");

    /* lerp(a, b, t) = a + (b - a) * t */
    VML_FuncB *f = vml_func_begin(m, "lerp", "(a: float, b: float, t: float): float");
    VML_Val a = vml_b_param(f, 0), b = vml_b_param(f, 1), t = vml_b_param(f, 2);
    vml_b_return(f, vml_b_add(f, a, vml_b_mul(f, vml_b_sub(f, b, a), t)));
    CHECK(vml_func_end(f) == VML_OK, "built lerp");

    /* make(x, y): Vec2 — a local struct, field assignment */
    f = vml_func_begin(m, "make", "(x: float, y: float): Vec2");
    VML_Val out = vml_b_var(f, "Vec2", -1);
    vml_b_assign(f, vml_b_field(f, out, "x"), vml_b_param(f, 0));
    vml_b_assign(f, vml_b_field(f, out, "y"), vml_b_param(f, 1));
    vml_b_return(f, out);
    CHECK(vml_func_end(f) == VML_OK, "built make");

    /* sum_to(n): loop with a mutable local */
    f = vml_func_begin(m, "sum_to", "(n: int): int");
    VML_Val i = vml_b_var(f, "int", vml_b_int(f, 0));
    VML_Val acc = vml_b_var(f, "int", vml_b_int(f, 0));
    vml_b_while(f, vml_b_lt(f, i, vml_b_param(f, 0)));
    vml_b_assign(f, i, vml_b_add(f, i, vml_b_int(f, 1)));
    vml_b_assign(f, acc, vml_b_add(f, acc, i));
    vml_b_end(f);
    vml_b_return(f, acc);
    CHECK(vml_func_end(f) == VML_OK, "built sum_to");

    /* label(x): if / else, strings, method calls */
    f = vml_func_begin(m, "label", "(x: int): string");
    vml_b_if(f, vml_b_gt(f, vml_b_param(f, 0), vml_b_int(f, 9)));
    vml_b_return(f, vml_b_method(f, vml_b_string(f, "big \"one\""), "upper", NULL, 0));
    vml_b_else(f);
    vml_b_return(f, vml_b_string(f, "small"));
    vml_b_end(f);
    CHECK(vml_func_end(f) == VML_OK, "built label");

    /* A C function taking builder structs */
    CHECK(vml_module_func(m, "dot", "(a: Vec2, b: Vec2): float", host_dot) == VML_OK, "C function with struct params");

    /* Errors are caught while building */
    VML_FuncB *bad = vml_func_begin(m, "bad", "(x: NoSuchType): int");
    CHECK(vml_func_end(bad) == VML_ERR_TYPE, "unknown type rejected by the builder");
    CHECK(vml_struct_begin(m, "bad name") == NULL, "invalid struct name rejected");

    CHECK(vml_module_finish(m) == VML_OK, "finish geo");
}

static const char *GEO_SCRIPT =
    "import geo\n"
    "func run(): float {\n"
    "    let v = geo.make(3.0, 4.0)\n"
    "    let w: Vec2\n"
    "    w.x = 2.0\n"
    "    w.y = 1.0\n"
    "    let s = Shape.Rect(2.0, 3.0)\n"
    "    let area = match s {\n"
    "        Circle(r) => r * r,\n"
    "        Rect(a, b) => a * b,\n"
    "        Empty => 0.0\n"
    "    }\n"
    "    return geo.lerp(0.0, 10.0, 0.5) + geo.dot(v, w) + area\n"   /* 5 + 10 + 6 */
    "}\n"
    "func loops(): int { return geo.sum_to(100) }\n"
    "func names(): string { return geo.label(50) + \"/\" + geo.label(1) }\n";

static void builder_test(void) {
    printf("-- builder API\n");
    VML_State *L = vml_new(NULL);
    register_geo(L);
    if (vml_load_source(L, "geo_script.vml", GEO_SCRIPT) != VML_OK) {
        printf("FAIL: geo load: %s\n", vml_last_error(L));
        failures++;
        vml_free(L);
        return;
    }
    CHECK(vml_call(L, "run", "") == VML_OK && vml_result_float(L) == 21.0, "built functions + struct + enum + C struct params");
    CHECK(vml_call(L, "loops", "") == VML_OK && vml_result_int(L) == 5050, "built loop");
    CHECK(vml_call(L, "names", "") == VML_OK, "built strings");
    size_t len = 0;
    const char *s = vml_result_string(L, &len);
    CHECK(len == 15 && memcmp(s, "BIG \"ONE\"/small", 15) == 0, "built if / else + method call");

    /* Built modules survive bytecode images like everything else */
    void *img = NULL;
    size_t img_len = 0;
    CHECK(vml_save_image(L, &img, &img_len) == VML_OK, "save geo image");
    size_t ilen = 0;
    const char *iface = vml_image_interface(img, img_len, &ilen);
    CHECK(iface != NULL && strstr(iface, "func run(): float") != NULL && strstr(iface, "{") == NULL,
          "image carries the interface (signatures, no bodies)");
    VML_State *L2 = vml_new(NULL);
    register_geo(L2);
    CHECK(vml_load_image(L2, img, img_len) == VML_OK, "load geo image");
    CHECK(vml_call(L2, "run", "") == VML_OK && vml_result_float(L2) == 21.0, "geo from image");
    vml_free_buffer(L, img);
    vml_free(L2);

    /* A type error inside a built function is reported at load time */
    VML_State *L3 = vml_new(NULL);
    VML_Module *m = vml_module_new(L3, "oops");
    VML_FuncB *f = vml_func_begin(m, "wrong", "(x: int): string");
    vml_b_return(f, vml_b_param(f, 0));
    vml_func_end(f);
    vml_module_finish(m);
    VML_Result r = vml_load_source(L3, "o.vml", "import oops\nfunc main() { print(oops.wrong(1)) }\n");
    CHECK(r == VML_ERR_COMPILE, "type error in built function is a compile error");
    printf("   (expected) %s\n", vml_last_error(L3));
    vml_free(L3);
    vml_free(L);
}

int main(void) {
    printf("vml %s\n", vml_version());

    /* 1. Compile from source */
    num_entities = 0;
    VML_State *L = vml_new(NULL);
    register_game(L);
    vml_module_source(L, "util", UTIL_SRC);
    if (vml_load_source(L, "script.vml", SCRIPT) != VML_OK) {
        printf("load failed: %s\n", vml_last_error(L));
        return 1;
    }
    exercise(L, "compiled from source");

    VML_MemStats st;
    vml_gc_collect(L);
    vml_memory_stats(L, &st);
    CHECK(st.collections >= 1, "explicit collection ran");
    printf("   memory: %zu bytes, %u collections, %u live strings\n", st.heap_bytes, st.collections, st.live_strings);

    /* 2. Save a bytecode image and run it in a fresh state (no compiler involved) */
    void *img = NULL;
    size_t img_len = 0;
    CHECK(vml_save_image(L, &img, &img_len) == VML_OK, "save image");
    vml_free(L);

    num_entities = 0;
    VML_State *L2 = vml_new(NULL);
    register_game(L2);
    VML_Result r = vml_load_image(L2, img, img_len);
    CHECK(r == VML_OK, "load image");
    if (r != VML_OK) printf("   %s\n", vml_last_error(L2));
    else exercise(L2, "loaded from bytecode image");

    /* 3. An image needs its host modules */
    VML_State *L3 = vml_new(NULL);
    r = vml_load_image(L3, img, img_len);
    CHECK(r == VML_ERR_NOT_FOUND, "image without host module is rejected");
    printf("   (expected) %s\n", vml_last_error(L3));
    vml_free(L3);
    vml_free_buffer(L2, img);
    vml_free(L2);

    /* 4. Corrupt images and compile errors are reported, not crashed on */
    VML_State *L4 = vml_new(NULL);
    CHECK(vml_load_image(L4, "garbage!", 8) == VML_ERR_FORMAT, "garbage image rejected");
    vml_free(L4);
    VML_State *L5 = vml_new(NULL);
    r = vml_load_source(L5, "bad.vml", "func f() {\n    let x = (1 +\n}\n");
    CHECK(r == VML_ERR_COMPILE && strstr(vml_last_error(L5), "bad.vml:3") != NULL, "compile error with location");
    printf("   (expected) %s\n", vml_last_error(L5));
    vml_free(L5);

    /* 5. Builder API: struct, enum and functions made from C */
    builder_test();

    if (failures == 0) printf("Embed API test: PASS\n");
    return failures == 0 ? 0 : 1;
}
