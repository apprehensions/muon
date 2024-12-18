/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/output.h"
#include "buf_size.h"
#include "cmd_install.h"
#include "cmd_subprojects.h"
#include "cmd_test.h"
#include "embedded.h"
#include "external/libarchive.h"
#include "external/libcurl.h"
#include "external/libpkgconf.h"
#include "external/samurai.h"
#include "lang/analyze.h"
#include "lang/fmt.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/serial.h"
#include "meson_opts.h"
#include "options.h"
#include "opts.h"
#include "platform/init.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tracy.h"
#include "ui.h"
#include "version.h"
#include "vsenv.h"

static bool
ensure_in_build_dir(void)
{
	if (!fs_dir_exists(output_path.private_dir)) {
		LOG_E("this subcommand must be run from a build directory");
		return false;
	}

	return true;
}

static bool
load_obj_from_serial_dump(struct workspace *wk, const char *path, obj *res)
{
	bool ret = false;
	FILE *f;
	if (!(f = fs_fopen(path, "rb"))) {
		return false;
	}

	if (!serial_load(wk, res, f)) {
		LOG_E("failed to load environment data");
		goto ret;
	}

	ret = true;
ret:
	if (!fs_fclose(f)) {
		ret = false;
	}
	return ret;
}

static bool
cmd_exe(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *feed;
		const char *capture;
		const char *environment;
		const char *args;
		const char *const *cmd;
		const char *remove_before_running;
	} opts = { 0 };

	OPTSTART("f:c:e:a:R:") {
	case 'f': opts.feed = optarg; break;
	case 'c': opts.capture = optarg; break;
	case 'e': opts.environment = optarg; break;
	case 'a': opts.args = optarg; break;
	case 'R': opts.remove_before_running = optarg; break;
	}
	OPTEND(argv[argi],
		" <cmd> [arg1[ arg2[...]]]",
		"  -f <file> - feed file to input\n"
		"  -c <file> - capture output to file\n"
		"  -e <file> - load environment from data file\n"
		"  -a <file> - load arguments from data file\n"
		"  -R <file> - remove file if it exists before executing the command\n",
		NULL,
		-1)

	if (argi >= argc && !opts.args) {
		LOG_E("missing command");
		return false;
	} else if (argi < argc && opts.args) {
		LOG_E("command cannot be specified by trailing arguments *and* -a");
		return false;
	}

	opts.cmd = (const char *const *)&argv[argi];
	++argi;

	if (opts.remove_before_running && fs_exists(opts.remove_before_running)) {
		if (!fs_remove(opts.remove_before_running)) {
			return false;
		}
	}

	bool ret = false;
	struct run_cmd_ctx ctx = { 0 };
	ctx.stdin_path = opts.feed;

	if (!opts.capture) {
		ctx.flags |= run_cmd_ctx_flag_dont_capture;
	}

	struct workspace wk;
	bool initialized_workspace = false, allocated_argv = false;

	const char *envstr = NULL;
	uint32_t envc = 0;
	if (opts.environment) {
		initialized_workspace = true;
		workspace_init_bare(&wk);

		obj env;
		if (!load_obj_from_serial_dump(&wk, opts.environment, &env)) {
			goto ret;
		}

		env_to_envstr(&wk, &envstr, &envc, env);
	}

	if (opts.args) {
		if (!initialized_workspace) {
			initialized_workspace = true;
			workspace_init_bare(&wk);
		}

		obj args;
		if (!load_obj_from_serial_dump(&wk, opts.args, &args)) {
			goto ret;
		}

		const char *argstr;
		uint32_t argc;
		join_args_argstr(&wk, &argstr, &argc, args);

		argstr_to_argv(argstr, argc, NULL, (char *const **)&opts.cmd);
		allocated_argv = true;
	}

	if (!run_cmd_argv(&ctx, (char *const *)opts.cmd, envstr, envc)) {
		LOG_E("failed to run command: %s", ctx.err_msg);
		goto ret;
	}

	if (ctx.status != 0) {
		if (opts.capture) {
			fputs(ctx.err.buf, stderr);
		}
		goto ret;
	}

	if (opts.capture) {
		ret = fs_write(opts.capture, (uint8_t *)ctx.out.buf, ctx.out.len);
	} else {
		ret = true;
	}
ret:
	run_cmd_ctx_destroy(&ctx);
	if (initialized_workspace) {
		workspace_destroy_bare(&wk);
	}
	if (allocated_argv) {
		z_free((void *)opts.cmd);
	}
	return ret;
}

static bool
cmd_check(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *filename;
		bool print_ast, print_dis;
		enum vm_compile_mode compile_mode;
	} opts = { 0 };

	OPTSTART("pdm:") {
	case 'p': opts.print_ast = true; break;
	case 'd': opts.print_dis = true; break;
	case 'm': {
		const char *p, *chars = "xf";
		for (p = optarg; *p; ++p) {
			if (!strchr(chars, *p)) {
				LOG_E("invalid mode '%c', must be one of %s", *p, chars);
				return false;
			}

			switch (*p) {
			case 'x': opts.compile_mode |= vm_compile_mode_language_extended; break;
			case 'f': opts.compile_mode |= vm_compile_mode_fmt; break;
			default: UNREACHABLE;
			}
		}
		break;
	}
	}
	OPTEND(argv[argi],
		" <filename>",
		"  -p - print parsed ast\n"
		"  -d - print dissasembly\n"
		"  -m <mode> - parse with parse mode <mode>\n",
		NULL,
		1)

	opts.filename = argv[argi];

	bool ret = false;

	struct workspace wk;
	workspace_init_bare(&wk);

	arr_push(&wk.vm.src, &(struct source){ 0 });
	struct source *src = arr_get(&wk.vm.src, 0);

	if (!fs_read_entire_file(opts.filename, src)) {
		goto ret;
	}

	if (opts.print_ast) {
		struct node *n;
		if (!(n = parse(&wk, src, opts.compile_mode))) {
			goto ret;
		}

		if (opts.compile_mode & vm_compile_mode_fmt) {
			print_fmt_ast(&wk, n);
		} else {
			print_ast(&wk, n);
		}
	} else {
		uint32_t _entry;
		if (!vm_compile(&wk, src, opts.compile_mode, &_entry)) {
			goto ret;
		}

		if (opts.print_dis) {
			vm_dis(&wk);
		}
	}

	ret = true;
ret:
	fs_source_destroy(src);
	workspace_destroy(&wk);
	return ret;
}

static bool
cmd_analyze(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct az_opts opts = {
		.enabled_diagnostics = az_diagnostic_unused_variable | az_diagnostic_dead_code
				       | az_diagnostic_redirect_script_error,
	};

	enum {
		action_trace,
		action_define,
		action_default,
	} action = action_default;

	static const struct command commands[] = {
		[action_trace] = { "trace", 0, "print a tree of all meson source files that are evaluated" },
		[action_define] = { "define <var>", 0, "lookup the definition of a variable" },
		0,
	};

	OPTSTART("luqO:W:i:") {
	case 'i': opts.internal_file = optarg; break;
	case 'l':
		opts.subdir_error = true;
		opts.replay_opts |= error_diagnostic_store_replay_dont_include_sources;
		break;
	case 'O': opts.file_override = optarg; break;
	case 'q': opts.replay_opts |= error_diagnostic_store_replay_errors_only; break;
	case 'W': {
		bool enable = true;
		const char *name = optarg;
		if (str_startswith(&WKSTR(optarg), &WKSTR("no-"))) {
			enable = false;
			name += 3;
		}

		if (strcmp(name, "list") == 0) {
			az_print_diagnostic_names();
			return true;
		} else if (strcmp(name, "error") == 0) {
			opts.replay_opts |= error_diagnostic_store_replay_werror;
		} else {
			enum az_diagnostic d;
			if (!az_diagnostic_name_to_enum(name, &d)) {
				LOG_E("invalid diagnostic name '%s'", name);
				return false;
			}

			if (enable) {
				opts.enabled_diagnostics |= d;
			} else {
				opts.enabled_diagnostics &= ~d;
			}
		}
		break;
	}
	}
	OPTEND(argv[argi],
		"",
		"  -l - optimize output for editor linter plugins\n"
		"  -q - only report errors\n"
		"  -O <path> - read project file with matching path from stdin\n"
		"  -i <path> - analyze the single file <path> in internal mode\n"
		"  -W [no-]<diagnostic> - enable or disable diagnostics\n"
		"  -W list - list available diagnostics\n"
		"  -W error - turn all warnings into errors\n",
		commands,
		-1);

	uint32_t cmd_i = action_default;
	if (!find_cmd(commands, &cmd_i, argc, argi, argv, true)) {
		return false;
	}
	if (cmd_i != action_default) {
		++argi;
	}
	action = cmd_i;

	if (action == action_define) {
		if (!check_operands(argc, argi, 1)) {
			return false;
		}
	} else {
		if (!check_operands(argc, argi, 0)) {
			return false;
		}
	}

	switch (action) {
	case action_default: break;
	case action_trace: {
		opts.eval_trace = true;
		break;
	}
	case action_define: {
		opts.get_definition_for = argv[argi];
		break;
	}
	}

	if (opts.internal_file && opts.file_override) {
		LOG_E("-i and -O are mutually exclusive");
		return false;
	}

	SBUF_manual(abs);
	if (opts.file_override) {
		path_make_absolute(NULL, &abs, opts.file_override);
		opts.file_override = abs.buf;
	}

	bool res;
	res = do_analyze(&opts);

	if (opts.file_override) {
		sbuf_destroy(&abs);
	}
	return res;
}

static bool
cmd_options(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct list_options_opts opts = { 0 };
	OPTSTART("am") {
	case 'a': opts.list_all = true; break;
	case 'm': opts.only_modified = true; break;
	}
	OPTEND(argv[argi],
		"",
		"  -a - list all options\n"
		"  -m - list only modified options\n",
		NULL,
		0)

	return list_options(&opts);
}

static bool
cmd_summary(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	}
	OPTEND(argv[argi], "", "", NULL, 0)

	if (!ensure_in_build_dir()) {
		return false;
	}

	SBUF_manual(path);
	path_join(0, &path, output_path.private_dir, output_path.summary);

	bool ret = false;
	struct source src = { 0 };
	if (!fs_read_entire_file(path.buf, &src)) {
		goto ret;
	}

	fwrite(src.src, 1, src.len, stdout);

	ret = true;
ret:
	sbuf_destroy(&path);
	fs_source_destroy(&src);
	return ret;
}

static bool
cmd_info(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	LOG_W("the info subcommand has been deprecated, please use options / summary directly");

	static const struct command commands[] = {
		{ "options", cmd_options, "list project options" },
		{ "summary", cmd_summary, "print a configured project's summary" },
		0,
	};

	OPTSTART("") {
	}
	OPTEND(argv[argi], "", "", commands, -1);

	uint32_t cmd_i;
	if (!find_cmd(commands, &cmd_i, argc, argi, argv, false)) {
		return false;
	}

	return commands[cmd_i].cmd(0, argc, argi, argv);
}

static bool
cmd_eval(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init_bare(&wk);

	const char *filename;
	bool embedded = false;

	OPTSTART("esb:") {
	case 'e': embedded = true; break;
	case 's': {
		wk.vm.disable_fuzz_unsafe_functions = true;
		break;
	}
	case 'b': {
		vm_dbg_push_breakpoint_str(&wk, optarg);
		break;
	}
	}
	OPTEND(argv[argi],
		" <filename> [args]",
		"  -e - lookup <filename> as an embedded script\n"
		"  -s - disable functions that are unsafe to be called at random\n",
		NULL,
		-1)

	if (argi >= argc) {
		LOG_E("missing required filename argument");
		return false;
	}

	filename = argv[argi];

	bool ret = false;

	struct source src = { 0 };

	machine_init();

	wk.vm.lang_mode = language_internal;

	if (embedded) {
		if (!(embedded_get(filename, &src))) {
			LOG_E("failed to find '%s' in embedded sources", filename);
			goto ret;
		}
	} else {
		if (!fs_read_entire_file(filename, &src)) {
			goto ret;
		}
	}

	{ // populate argv array
		obj argv_obj;
		make_obj(&wk, &argv_obj, obj_array);
		wk.vm.behavior.assign_variable(&wk, "argv", argv_obj, 0, assign_local);

		uint32_t i;
		for (i = argi; i < argc; ++i) {
			obj_array_push(&wk, argv_obj, make_str(&wk, argv[i]));
		}
	}

	obj res;
	if (!eval(&wk, &src, build_language_meson, 0, &res)) {
		goto ret;
	}

	ret = true;
ret:
	workspace_destroy(&wk);
	return ret;
}

static bool
cmd_repl(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init(&wk);
	wk.vm.lang_mode = language_internal;

	obj id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);

	repl(&wk, false);

	workspace_destroy(&wk);
	return true;
}

static bool
cmd_dump_signatures(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	}
	OPTEND(argv[argi], "", "", NULL, 0)

	struct workspace wk;
	workspace_init(&wk);

	obj id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);
	if (!setup_project_options(&wk, NULL)) {
		UNREACHABLE;
	}

	dump_function_signatures(&wk);

	workspace_destroy(&wk);
	return true;
}

static bool
cmd_dump_toolchains(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct obj_compiler comp = { 0 };
	bool set_linker, set_static_linker;

	const char *n1_args[32] = { "<value1>", "<value2>" };
	struct args n1 = { n1_args, 2 };
	struct toolchain_dump_opts opts = {
		.s1 = "<value1>",
		.s2 = "<value2>",
		.b1 = true,
		.i1 = 0,
		.n1 = &n1,
	};

	OPTSTART("t:s:") {
	case 't': {
		if (strcmp(optarg, "list") == 0) {
			printf("registered toolchains:\n");
			enum toolchain_component component;
			for (component = 0; component < toolchain_component_count; ++component) {
				const struct toolchain_id *list = 0;
				uint32_t i, count = 0;
				switch (component) {
				case toolchain_component_compiler:
					list = compiler_type_name;
					count = compiler_type_count;
					break;
				case toolchain_component_linker:
					list = linker_type_name;
					count = linker_type_count;
					break;
				case toolchain_component_static_linker:
					list = static_linker_type_name;
					count = static_linker_type_count;
					break;
				}

				printf("  %s\n", toolchain_component_to_s(component));
				for (i = 0; i < count; ++i) {
					printf("    %s\n", list[i].id);
				}
			}
			return true;
		}

		char *sep;
		if (!(sep = strchr(optarg, '='))) {
			LOG_E("invalid type: %s", optarg);
			return false;
		}

		*sep = 0;

		enum toolchain_component component;
		const char *type = sep + 1;

		if (!toolchain_component_from_s(optarg, &component)) {
			LOG_E("unknown toolchain component: %s", optarg);
			return false;
		}

		switch (component) {
		case toolchain_component_compiler:
			if (!compiler_type_from_s(type, &comp.type[component])) {
				LOG_E("unknown compiler type: %s", type);
				return false;
			}

			if (!set_linker) {
				comp.type[toolchain_component_linker] = compilers[comp.type[component]].default_linker;
			}
			if (!set_static_linker) {
				comp.type[toolchain_component_static_linker]
					= compilers[comp.type[component]].default_static_linker;
			}
			break;
		case toolchain_component_linker:
			set_linker = true;
			if (!linker_type_from_s(type, &comp.type[component])) {
				LOG_E("unknown linker type: %s", type);
				return false;
			}
			break;
		case toolchain_component_static_linker:
			set_static_linker = true;
			if (!static_linker_type_from_s(type, &comp.type[component])) {
				LOG_E("unknown static_linker type: %s", type);
				return false;
			}
			break;
		}

		break;
	}
	case 's': {
		char *sep;
		if (!(sep = strchr(optarg, '='))) {
			LOG_E("invalid argument setting: %s", optarg);
			return false;
		}

		*sep = 0;
		++sep;

		if (strcmp(optarg, "s1") == 0) {
			opts.s1 = sep;
		} else if (strcmp(optarg, "s2") == 0) {
			opts.s2 = sep;
		} else if (strcmp(optarg, "b1") == 0) {
			if (strcmp(sep, "true") == 0) {
				opts.b1 = true;
			} else if (strcmp(sep, "false") == 0) {
				opts.b1 = false;
			} else {
				LOG_E("invalid value for bool: %s", sep);
				return false;
			}
		} else if (strcmp(optarg, "i1") == 0) {
			int64_t res;
			if (!str_to_i(&WKSTR(sep), &res, false)) {
				LOG_E("invalid value for integer: %s", sep);
				return false;
			}

			opts.i1 = res;
		} else if (strcmp(optarg, "n1") == 0) {
			n1.len = 0;

			while (*sep) {
				if (n1.len >= ARRAY_LEN(n1_args)) {
					LOG_E("too many arguments for n1 value");
					return false;
				}

				n1.args[n1.len] = sep;
				++n1.len;

				sep = strchr(sep, ',');
				if (!sep) {
					break;
				}
				*sep = 0;
				++sep;
			}
		} else {
			LOG_E("invalid setting name: %s", optarg);
			return false;
		}

		break;
	}
	}
	OPTEND(argv[argi],
		"",
		"  -t <component>=<type>|list - set the type for a component or list all component types\n"
		"  -s <key>=<val> - set the value for a template argument\n",
		NULL,
		0)

	struct workspace wk;
	workspace_init(&wk);

	obj id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);
	if (!setup_project_options(&wk, NULL)) {
		UNREACHABLE;
	}

	printf("compiler: %s, linker: %s, static_linker: %s\n",
		compiler_type_name[comp.type[toolchain_component_compiler]].id,
		linker_type_name[comp.type[toolchain_component_linker]].id,
		static_linker_type_name[comp.type[toolchain_component_static_linker]].id);
	printf("template arguments: s1: \"%s\", s2: \"%s\", b1: %s, i1: %d, n1: {",
		opts.s1,
		opts.s2,
		opts.b1 ? "true" : "false",
		opts.i1);
	for (uint32_t i = 0; i < opts.n1->len; ++i) {
		printf("\"%s\"", opts.n1->args[i]);
		if (i + 1 < opts.n1->len) {
			printf(", ");
		}
	}
	printf("}\n");

	toolchain_dump(&wk, &comp, &opts);

	workspace_destroy(&wk);
	return true;
}

static bool
cmd_internal(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "eval", cmd_eval, "evaluate a file" },
		{ "exe", cmd_exe, "run an external command" },
		{ "repl", cmd_repl, "start a meson language repl" },
		{ "dump_funcs", cmd_dump_signatures, "output all supported functions and arguments" },
		{ "dump_toolchains", cmd_dump_toolchains, "output toolchain arguments" },
		0,
	};

	OPTSTART("") {
	}
	OPTEND(argv[argi], "", "", commands, -1);

	uint32_t cmd_i;
	if (!find_cmd(commands, &cmd_i, argc, argi, argv, false)) {
		return false;
	}

	return commands[cmd_i].cmd(0, argc, argi, argv);
}

static bool
cmd_samu(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	return samu_main(argc - argi, (char **)&argv[argi], 0);
}

static bool
cmd_test(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct test_options test_opts = { 0 };

	if (strcmp(argv[argi], "benchmark") == 0) {
		test_opts.cat = test_category_benchmark;
		test_opts.print_summary = true;
	}

	OPTSTART("s:d:Sfj:lvRe:o:") {
	case 'l': test_opts.list = true; break;
	case 'e': test_opts.setup = optarg; break;
	case 's':
		if (test_opts.suites_len > MAX_CMDLINE_TEST_SUITES) {
			LOG_E("too many -s options (max: %d)", MAX_CMDLINE_TEST_SUITES);
			return false;
		}
		test_opts.suites[test_opts.suites_len] = optarg;
		++test_opts.suites_len;
		break;
	case 'd':
		if (strcmp(optarg, "auto") == 0) {
			test_opts.display = test_display_auto;
		} else if (strcmp(optarg, "dots") == 0) {
			test_opts.display = test_display_dots;
		} else if (strcmp(optarg, "bar") == 0) {
			test_opts.display = test_display_bar;
		} else {
			LOG_E("invalid progress display mode '%s'", optarg);
			return false;
		}
		break;
	case 'o':
		if (strcmp(optarg, "term") == 0) {
			test_opts.output = test_output_term;
		} else if (strcmp(optarg, "html") == 0) {
			test_opts.output = test_output_html;
		} else if (strcmp(optarg, "json") == 0) {
			test_opts.output = test_output_json;
		} else {
			LOG_E("invalid progress display mode '%s'", optarg);
			return false;
		}
		break;
	case 'f': test_opts.fail_fast = true; break;
	case 'S': test_opts.print_summary = true; break;
	case 'j': {
		char *endptr;
		unsigned long n = strtoul(optarg, &endptr, 10);

		if (n > UINT32_MAX || !*optarg || *endptr) {
			LOG_E("invalid number of jobs: %s", optarg);
			return false;
		}

		test_opts.jobs = n;
		break;
	}
	case 'v': ++test_opts.verbosity; break;
	case 'R': test_opts.no_rebuild = true; break;
	}
	OPTEND(argv[argi],
		" [test [test [...]]]",
		"  -d <mode> - change progress display mode (auto|dots|bar)\n"
		"  -o <mode> - set output mode (term|html|json)\n"
		"  -e <setup> - use test setup <setup>\n"
		"  -f - fail fast; exit after first failure\n"
		"  -j <jobs> - set the number of test workers\n"
		"  -l - list tests that would be run\n"
		"  -R - disable automatic rebuild\n"
		"  -S - print a summary with elapsed time\n"
		"  -s <suite> - only run items in <suite>, may be passed multiple times\n"
		"  -v - increase verbosity, may be passed twice\n",
		NULL,
		-1)

	if (!ensure_in_build_dir()) {
		return false;
	}

	test_opts.tests = &argv[argi];
	test_opts.tests_len = argc - argi;

	return tests_run(&test_opts, argv[0]);
}

static bool
cmd_install(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct install_options opts = {
		.destdir = getenv("DESTDIR"),
	};

	OPTSTART("nd:") {
	case 'n': opts.dry_run = true; break;
	case 'd': opts.destdir = optarg; break;
	}
	OPTEND(argv[argi],
		"",
		"  -n - dry run\n"
		"  -d <destdir> - set destdir\n",
		NULL,
		0)

	if (!ensure_in_build_dir()) {
		return false;
	}

	return install_run(&opts);
}

static bool
cmd_setup(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	TracyCZoneAutoS;
	bool res = false;
	struct workspace wk;
	workspace_init_bare(&wk);
	workspace_init_runtime(&wk);

	uint32_t original_argi = argi + 1;

	OPTSTART("D:b:") {
	case 'D':
		if (!parse_and_set_cmdline_option(&wk, optarg)) {
			goto ret;
		}
		break;
	case 'b': {
		vm_dbg_push_breakpoint_str(&wk, optarg);
		break;
	}
	}
	OPTEND(argv[argi],
		" <build dir>",
		"  -D <option>=<value> - set project options\n"
		"  -b <breakpoint> - set breakpoint\n",
		NULL,
		1)

	const char *build = argv[argi];
	++argi;

	if (!workspace_do_setup(&wk, build, argv[0], argc - original_argi, &argv[original_argi])) {
		goto ret;
	}

	res = true;
ret:
	workspace_destroy(&wk);
	TracyCZoneAutoE;
	return res;
}

static bool
cmd_format(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	if (strcmp(argv[argi], "fmt_unstable") == 0) {
		LOG_W("the subcommand name fmt_unstable is deprecated, please use fmt instead");
	}

	struct {
		char *const *filenames;
		const char *cfg_path;
		bool in_place, check_only, editorconfig;
	} opts = { 0 };

	OPTSTART("ic:qe") {
	case 'i': opts.in_place = true; break;
	case 'c': opts.cfg_path = optarg; break;
	case 'q': opts.check_only = true; break;
	case 'e': opts.editorconfig = true; break;
	}
	OPTEND(argv[argi],
		" <file>[ <file>[...]]",
		"  -q - exit with 1 if files would be modified by muon fmt\n"
		"  -i - format files in-place\n"
		"  -c <muon_fmt.ini> - read configuration from muon_fmt.ini\n"
		"  -e - try to read configuration from .editorconfig\n",
		NULL,
		-1)

	if (opts.in_place && opts.check_only) {
		LOG_E("-q and -i are mutually exclusive");
		return false;
	}

	log_set_file(stderr);

	opts.filenames = &argv[argi];
	const uint32_t num_files = argc - argi;

	bool ret = true;
	bool opened_out;
	FILE *out;
	uint32_t i;
	for (i = 0; i < num_files; ++i) {
		bool fmt_ret = true;
		opened_out = false;

		struct source src = { 0 };
		if (!fs_read_entire_file(opts.filenames[i], &src)) {
			ret = false;
			goto cont;
		}

		if (opts.in_place) {
			if (!(out = fs_fopen(opts.filenames[i], "wb"))) {
				ret = false;
				goto cont;
			}
			opened_out = true;
		} else if (opts.check_only) {
			out = NULL;
		} else {
			out = stdout;
		}

		fmt_ret = fmt(&src, out, opts.cfg_path, opts.check_only, opts.editorconfig);
cont:
		if (opened_out) {
			fs_fclose(out);

			if (!fmt_ret) {
				fs_write(opts.filenames[i], (const uint8_t *)src.src, src.len);
			}
		}
		fs_source_destroy(&src);
		ret &= fmt_ret;
	}

	return ret;
}

static bool
cmd_version(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	printf("muon %s%s%s\nmeson compatibility version %s\nenabled features:\n",
		muon_version.version,
		*muon_version.vcs_tag ? "-" : "",
		muon_version.vcs_tag,
		muon_version.meson_compat);

	const struct {
		const char *name;
		bool enabled;
	} feature_names[] = {
		{ "libcurl", have_libcurl },
		{ "libpkgconf", have_libpkgconf },
		{ "libarchive", have_libarchive },
		{ "samurai", have_samurai },
#ifdef TRACY_ENABLE
		{ "tracy", true },
#endif
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(feature_names); ++i) {
		if (feature_names[i].enabled) {
			printf("  %s\n", feature_names[i].name);
		}
	}

	return true;
}

static bool cmd_main(void *_ctx, uint32_t argc, uint32_t argi, char *argv[]);

static bool
cmd_meson(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	++argi;

	struct workspace wk;
	char **new_argv;
	uint32_t new_argc, new_argi;
	workspace_init_bare(&wk);
	if (!translate_meson_opts(&wk, argc, argi, (char **)argv, &new_argc, &new_argi, &new_argv)) {
		return false;
	}

	argi = new_argi;
	argc = new_argc;
	argv = new_argv;

	bool res = cmd_main(0, argc, argi, (char **)argv);

	workspace_destroy(&wk);
	z_free(new_argv);

	return res;
}

static bool
cmd_ui(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	}
	OPTEND(argv[argi], "", "", 0, 0);

	return ui_main();
}

static bool
cmd_main(void *_ctx, uint32_t argc, uint32_t argi, char *argv[])
{
	const struct command commands[] = {
		{ "analyze", cmd_analyze, "run a static analyzer on the current project." },
		{ "benchmark", cmd_test, "run benchmarks" },
		{ "check", cmd_check, "check if a meson file parses" },
		{ "fmt", cmd_format, "format meson source file" },
		{ "fmt_unstable", cmd_format, NULL },
		{ "info", cmd_info, NULL },
		{ "install", cmd_install, "install project" },
		{ "internal", cmd_internal, "internal subcommands" },
		{ "meson", cmd_meson, "meson compatible cli proxy" },
		{ "options", cmd_options, "list project options" },
		{ "samu", cmd_samu, have_samurai ? "run samurai" : NULL },
		{ "setup", cmd_setup, "setup a build directory" },
		{ "subprojects", cmd_subprojects, "manage subprojects" },
		{ "summary", cmd_summary, "print a configured project's summary" },
		{ "test", cmd_test, "run tests" },
		{ "ui", cmd_ui, "ui" },
		{ "version", cmd_version, "print version information" },
		{ 0 },
	};

	bool res = false;
	SBUF_manual(argv0);

	OPTSTART("vC:e:") {
	case 'v': log_set_lvl(log_debug); break;
	case 'C': {
		// fix argv0 here since if it is a relative path it will be
		// wrong after chdir
		if (!path_is_basename(argv[0])) {
			path_make_absolute(NULL, &argv0, argv[0]);
			argv[0] = argv0.buf;
		}

		if (!path_chdir(optarg)) {
			return false;
		}
		break;
	}
	case 'e': {
		if (strcmp(optarg, "vs") != 0) {
			LOG_E("unsupported env \"%s\", supported envs are: vs", optarg);
			return false;
		}

		if (!vsenv_setup(true)) {
			return false;
		}
		break;
	}
	}
	OPTEND(argv[0],
		"",
		"  -v - turn on debug messages\n"
		"  -C <path> - chdir to path\n"
		"  -e <env> - load environment\n",
		commands,
		-1)

	uint32_t cmd_i;
	if (!find_cmd(commands, &cmd_i, argc, argi, argv, false)) {
		goto ret;
	}

	res = commands[cmd_i].cmd(0, argc, argi, argv);

ret:
	sbuf_destroy(&argv0);
	return res;
}

int
main(int argc, char *argv[])
{
	platform_init();

	log_init();
	log_set_lvl(log_info);

	path_init();

	compilers_init();

	bool res;
	bool meson_compat = false;

	{
		SBUF(basename);
		path_basename(NULL, &basename, argv[0]);
		meson_compat = strcmp(basename.buf, "meson") == 0 && (argc < 2 || strcmp(argv[1], "internal") != 0);
		sbuf_destroy(&basename);
	}

	if (meson_compat) {
		res = cmd_meson(0, argc, 0, argv);
	} else {
		res = cmd_main(0, argc, 0, argv);
	}

	int ret = res ? 0 : 1;

	path_deinit();
	return ret;
}
