#ifndef MUON_COMPILERS_H
#define MUON_COMPILERS_H

#include <stdbool.h>
#include <stdint.h>
struct workspace;

enum compiler_type {
	compiler_posix,
	compiler_gcc,
	compiler_clang,
	compiler_type_count,
};

enum linker_type {
	linker_posix,
	linker_gcc,
	linker_type_count,
};

enum compiler_language {
	compiler_language_c,
	compiler_language_c_hdr,
	compiler_language_cpp,
	compiler_language_cpp_hdr,
	compiler_language_c_obj,
	compiler_language_count,
};

enum compiler_deps_type {
	compiler_deps_none,
	compiler_deps_gcc,
	compiler_deps_msvc,
};

enum compiler_optimization_lvl {
	compiler_optimization_lvl_0,
	compiler_optimization_lvl_1,
	compiler_optimization_lvl_2,
	compiler_optimization_lvl_3,
	compiler_optimization_lvl_g,
	compiler_optimization_lvl_s,
};

enum compiler_warning_lvl {
	compiler_warning_lvl_0,
	compiler_warning_lvl_1,
	compiler_warning_lvl_2,
	compiler_warning_lvl_3,
};

typedef const struct args *((*compiler_get_arg_func_0)(void));
typedef const struct args *((*compiler_get_arg_func_1i)(uint32_t));
typedef const struct args *((*compiler_get_arg_func_1s)(const char *));
typedef const struct args *((*compiler_get_arg_func_2s)(const char *, const char *));

struct compiler {
	struct {
		compiler_get_arg_func_2s deps;
		compiler_get_arg_func_0 compile_only;
		compiler_get_arg_func_0 preprocess_only;
		compiler_get_arg_func_1s output;
		compiler_get_arg_func_1i optimization;
		compiler_get_arg_func_0 debug;
		compiler_get_arg_func_1i warning_lvl;
		compiler_get_arg_func_0 werror;
		compiler_get_arg_func_1s set_std;
		compiler_get_arg_func_1s include;
		compiler_get_arg_func_1s include_system;
		compiler_get_arg_func_0 pic;
		compiler_get_arg_func_1s sanitize;
	} args;
	enum compiler_deps_type deps;
	enum linker_type linker;
};

struct linker {
	struct {
		compiler_get_arg_func_0 as_needed;
		compiler_get_arg_func_0 no_undefined;
		compiler_get_arg_func_0 start_group;
		compiler_get_arg_func_0 end_group;
		compiler_get_arg_func_0 shared;
		compiler_get_arg_func_1s soname;
		compiler_get_arg_func_1s rpath;
		compiler_get_arg_func_1s sanitize;
		compiler_get_arg_func_0 allow_shlib_undefined;
	} args;
};

struct language {
	bool is_header;
	bool is_linkable;
};

extern struct compiler compilers[];
extern struct linker linkers[];
extern const struct language languages[];

const char *compiler_type_to_s(enum compiler_type t);
const char *compiler_language_to_s(enum compiler_language l);
bool s_to_compiler_language(const char *s, enum compiler_language *l);
bool filename_to_compiler_language(const char *str, enum compiler_language *l);
bool compiler_detect(struct workspace *wk, uint32_t *comp, enum compiler_language lang);
void compilers_init(void);
#endif
