#include "posix.h"

#include "functions/common.h"
#include "functions/meson.h"
#include "interpreter.h"
#include "log.h"

static bool
func_meson_get_compiler(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs { kw_native, };
	static struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (!check_lang(wk, an[0].node, an[0].val)) {
		return false;
	}

	make_obj(wk, obj, obj_compiler);

	return true;
}

const struct func_impl_name impl_tbl_meson[] = {
	{ "get_compiler", func_meson_get_compiler },
	{ NULL, NULL },
};
