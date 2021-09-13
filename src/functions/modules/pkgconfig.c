#include "posix.h"

#include "functions/modules/pkgconfig.h"

static bool
func_module_pkgconfig_generate(struct workspace *wk, obj rcvr, uint32_t args_node, obj *obj)
{
	struct args_norm ao[] = { { obj_build_target }, ARG_TYPE_NULL };
	enum kwargs {
		kw_description,
		kw_extra_cflags,
		kw_filebase,
		kw_install_dir,
		kw_libraries,
		kw_libraries_private,
		kw_name,
		kw_subdirs,
		kw_requires,
		kw_requires_private,
		kw_url,
		kw_variables,
		kw_unescaped_variables,
		kw_uninstalled_variables,
		kw_unescaped_uninstalled_variables,
		kw_version,
		kw_dataonly,
		kw_conflicts,
	};
	struct args_kw akw[] = {
		[kw_description] = { "description", obj_string },
		[kw_extra_cflags] = { "extra_cflags", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_filebase] = { "filebase", obj_string },
		[kw_install_dir] = { "install_dir", obj_string },
		[kw_libraries] = { "libraries", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_libraries_private] = { "libraries_private", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_name] = { "name", obj_string },
		[kw_subdirs] = { "subdirs", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_requires] = { "requires", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_requires_private] = { "requires_private", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_url] = { "url", obj_string },
		[kw_variables] = { "variables", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_unescaped_variables] = { "variables", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_uninstalled_variables] = { "uninstalled_variables", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_unescaped_uninstalled_variables] = { "variables", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_version] = { "version", obj_string },
		[kw_dataonly] = { "dataonly", obj_bool },
		[kw_conflicts] = { "conflicts", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};
	if (!interp_args(wk, args_node, NULL, ao, akw)) {
		return false;
	}

	return true;
}
const struct func_impl_name impl_tbl_module_pkgconfig[] = {
	{ "generate", func_module_pkgconfig_generate },
	{ NULL, NULL },
};
