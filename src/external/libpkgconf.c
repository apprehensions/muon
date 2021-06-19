#include "posix.h"

#include <libpkgconf/libpkgconf.h>
#include <limits.h>
#include <string.h>

#include "filesystem.h"
#include "log.h"
#include "object.h"
#include "external/pkgconf.h"
#include "workspace.h"

const bool have_libpkgconf = true;

static pkgconf_client_t client;
static pkgconf_cross_personality_t *personality;
static const int maxdepth = 200;

static bool init = false;

static bool
error_handler(const char *msg, const pkgconf_client_t *client, const void *data)
{
	/* log_plain("%s", msg); */
	return true;
}

void
muon_pkgconf_init(void)
{
	// HACK: TODO: libpkgconf breaks if you try use it after deiniting a
	// client.  Also there are memory leaks abound.
	if (init) {
		return;
	}

	personality = pkgconf_cross_personality_default();
	pkgconf_client_init(&client, error_handler, NULL, personality);
	pkgconf_client_dir_list_build(&client, personality);
	pkgconf_client_set_flags(&client, PKGCONF_PKG_PKGF_SEARCH_PRIVATE);
	init = true;
}

void
muon_pkgconf_deinit(void)
{
	return;
	pkgconf_path_free(&personality->dir_list);
	pkgconf_path_free(&personality->filter_libdirs);
	pkgconf_path_free(&personality->filter_includedirs);
	pkgconf_client_deinit(&client);
}

static const char *
pkgconf_strerr(int err)
{
	switch (err) {
	case PKGCONF_PKG_ERRF_OK:
		return "ok";
	case PKGCONF_PKG_ERRF_PACKAGE_NOT_FOUND:
		return "not found";
	case PKGCONF_PKG_ERRF_PACKAGE_VER_MISMATCH:
		return "ver mismatch";
	case PKGCONF_PKG_ERRF_PACKAGE_CONFLICT:
		return "package conflict";
	case PKGCONF_PKG_ERRF_DEPGRAPH_BREAK:
		return "depgraph break";
	}

	return "unknown";
}


typedef unsigned int (*apply_func)(pkgconf_client_t *client,
	pkgconf_pkg_t *world, pkgconf_list_t *list, int maxdepth);

struct pkgconf_lookup_ctx {
	apply_func apply_func;
	struct workspace *wk;
	struct pkgconf_info *info;
	uint32_t libdirs;
};

struct find_lib_path_ctx {
	bool found;
	const char *name;
	char *buf;
};

static bool
check_lib_path(struct find_lib_path_ctx *ctx, const char *lib_path)
{
	static const char *ext[] = { ".a", ".so", NULL };
	uint32_t i;

	for (i = 0; ext[i]; ++i) {
		snprintf(ctx->buf, PATH_MAX, "%s/lib%s%s", lib_path, ctx->name, ext[i]);

		if (fs_file_exists(ctx->buf)) {
			ctx->found = true;
			return true;
		}
	}

	return false;
}

static enum iteration_result
find_lib_path_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct find_lib_path_ctx *ctx = _ctx;

	if (check_lib_path(ctx, wk_objstr(wk, val_id))) {
		return ir_done;
	}

	return ir_cont;
}

static const char *
find_lib_path(pkgconf_client_t *client, struct pkgconf_lookup_ctx *ctx, const char *name)
{
	static char buf[PATH_MAX + 1];
	struct find_lib_path_ctx find_lib_path_ctx = { .buf = buf, .name = name };
	pkgconf_node_t *node;
	pkgconf_path_t *path;

	PKGCONF_FOREACH_LIST_ENTRY(client->filter_libdirs.head, node) {
		path = node->data;

		if (check_lib_path(&find_lib_path_ctx, path->path)) {
			return buf;
		}
	}

	if (!obj_array_foreach(ctx->wk, ctx->libdirs, &find_lib_path_ctx, find_lib_path_iter)) {
		return NULL;
	} else if (!find_lib_path_ctx.found) {
		return NULL;
	}

	return buf;
}

static bool
apply_and_collect(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	struct pkgconf_lookup_ctx *ctx = _ctx;
	int err;
	pkgconf_node_t *node;
	pkgconf_list_t list = PKGCONF_LIST_INITIALIZER;
	uint32_t str;
	bool ret = true;

	err = ctx->apply_func(client, world, &list, maxdepth);
	if (err != PKGCONF_PKG_ERRF_OK) {
		LOG_W(log_misc, "apply_func failed: %s", pkgconf_strerr(err));
		ret = false;
		goto ret;
	}

	PKGCONF_FOREACH_LIST_ENTRY(list.head, node) {
		const pkgconf_fragment_t *frag = node->data;
		switch (frag->type) {
		case 'I':
			make_obj(ctx->wk, &str, obj_file)->dat.file = wk_str_push(ctx->wk, frag->data);
			obj_array_push(ctx->wk, ctx->info->includes, str);
			break;
		case 'L':
			make_obj(ctx->wk, &str, obj_string)->dat.str = wk_str_push(ctx->wk, frag->data);
			obj_array_push(ctx->wk, ctx->libdirs, str);
			break;
		case 'l': {
			const char *path;
			if (!(path = find_lib_path(client, ctx, frag->data))) {
				LOG_W(log_misc, "couldn't resolve lib '%s'", frag->data);
				ret = false;
				goto ret;
			}

			make_obj(ctx->wk, &str, obj_string)->dat.str = wk_str_push(ctx->wk, path);
			obj_array_push(ctx->wk, ctx->info->libs, str);
			break;
		}
		default:
			/* L(log_misc, "skipping unknown option: -'%d' '%s'", frag->type, frag->data); */
			break;
		}
	}

ret:
	pkgconf_fragment_free(&list);
	return ret;

}

static bool
apply_modversion(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	pkgconf_node_t *node;
	bool first = true;
	struct pkgconf_lookup_ctx *ctx = _ctx;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, node){
		if (!first) {
			assert(false && "there should only be one version for one package, right?");
		}
		first = false;

		pkgconf_dependency_t *dep = node->data;
		pkgconf_pkg_t *pkg = dep->match;

		if (pkg->version != NULL) {
			strncpy(ctx->info->version, pkg->version, MAX_VERSION_LEN);
		}
	}

	return true;
}

bool
muon_pkgconf_lookup(struct workspace *wk, const char *name, struct pkgconf_info *info)
{
	bool ret = true;
	pkgconf_list_t pkgq = PKGCONF_LIST_INITIALIZER;
	pkgconf_queue_push(&pkgq, name);

	struct pkgconf_lookup_ctx ctx = { .wk = wk, .info = info };

	if (!pkgconf_queue_apply(&client, &pkgq, apply_modversion, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

	make_obj(wk, &info->includes, obj_array);
	make_obj(wk, &info->libs, obj_array);
	make_obj(wk, &ctx.libdirs, obj_array);

	ctx.apply_func = pkgconf_pkg_libs;
	if (!pkgconf_queue_apply(&client, &pkgq, apply_and_collect, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

	ctx.apply_func = pkgconf_pkg_cflags;
	if (!pkgconf_queue_apply(&client, &pkgq, apply_and_collect, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

ret:
	pkgconf_queue_free(&pkgq);
	return ret;
}

struct pkgconf_get_variable_ctx {
	struct workspace *wk;
	const char *var;
	uint32_t *res;
};

static bool
apply_variable(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	struct pkgconf_get_variable_ctx *ctx = _ctx;
	pkgconf_node_t *node;
	bool first = true, found = false;
	const char *var;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, node){
		if (!first) {
			assert(false && "there should only be one iteration for one package, right?");
		}
		first = false;

		pkgconf_dependency_t *dep = node->data;
		pkgconf_pkg_t *pkg = dep->match;

		var = pkgconf_tuple_find(client, &pkg->vars, ctx->var);
		if (var != NULL) {
			*ctx->res = wk_str_push(ctx->wk, var);
			found = true;
		}
	}

	return found;
}

bool
muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, char *var, uint32_t *res)
{
	pkgconf_list_t pkgq = PKGCONF_LIST_INITIALIZER;
	pkgconf_queue_push(&pkgq, pkg_name);
	bool ret = true;

	struct pkgconf_get_variable_ctx ctx = { .wk = wk, .res = res, .var = var, };

	if (!pkgconf_queue_apply(&client, &pkgq, apply_variable, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

ret:
	pkgconf_queue_free(&pkgq);
	return ret;
}
