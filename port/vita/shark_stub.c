/* vitapoke: stubs for vitaShaRK's runtime GLSL compiler.
 *
 * vitaGL links shark_* for glShaderSource/glCompileShader and for the
 * fixed-function fallback that compiles a shader when a texture-combiner
 * state has no precompiled variant. We only ever use precompiled shaders,
 * so the real compiler is not linked: it needs SceShaccCg, which lives in
 * libshacccg.suprx and is not on a console unless the user extracts it
 * from a firmware update. Refusing to compile here keeps that off the list
 * of things a player has to install.
 *
 * Every function reports failure rather than aborting: a caller that asks
 * for a runtime shader gets a clean "no compiler" answer, and vitaGL's own
 * error handling takes it from there.
 */
#include <stddef.h>
#include <stdint.h>

typedef enum shark_opt { SHARK_OPT_UNUSED = -1 } shark_opt;
typedef enum shark_type { SHARK_TYPE_UNUSED = -1 } shark_type;
typedef enum shark_locale { SHARK_LOCALE_UNUSED = -1 } shark_locale;
typedef enum shark_log_level { SHARK_LOG_UNUSED = -1 } shark_log_level;
typedef enum shark_warn_level { SHARK_WARN_UNUSED = -1 } shark_warn_level;

int shark_init(const char *path) { (void)path; return -1; }
int shark_init_simple(const char *path) { (void)path; return -1; }
void shark_end(void) {}
void shark_set_allocators(void *(*m)(size_t), void (*f)(void *)) { (void)m; (void)f; }

void *shark_compile_shader_extended(const char *src, uint32_t *size, shark_type type,
                                    shark_opt opt, int32_t fastmath, int32_t fastprecision,
                                    int32_t fastint) {
	(void)src; (void)type; (void)opt; (void)fastmath; (void)fastprecision; (void)fastint;
	if (size) *size = 0;
	return NULL;
}
void *shark_compile_shader(const char *src, uint32_t *size, shark_type type) {
	(void)src; (void)type;
	if (size) *size = 0;
	return NULL;
}
void shark_clear_output(void) {}
const void *shark_get_internal_compile_output(void) { return NULL; }

void shark_install_log_cb(void (*cb)(const char *, shark_log_level, int)) { (void)cb; }
void shark_set_warnings_level(shark_warn_level level) { (void)level; }
void shark_set_shader_association_path(const char *path) { (void)path; }
void shark_set_locale(shark_locale locale) { (void)locale; }
