/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifdef __sun
#define __EXTENSIONS__
#endif

#include "args.c"
#include "backend/backend.c"
#include "backend/common_args.c"
#include "backend/ninja.c"
#include "backend/ninja/alias_target.c"
#include "backend/ninja/build_target.c"
#include "backend/ninja/custom_target.c"
#include "backend/ninja/rules.c"
#include "backend/output.c"
#include "cmd_install.c"
#include "cmd_test.c"
#include "coerce.c"
#include "compilers.c"
#include "datastructures/arr.c"
#include "datastructures/bucket_arr.c"
#include "datastructures/hash.c"
#include "datastructures/stack.c"
#include "embedded.c"
#include "error.c"
#include "external/bestline_null.c"
#include "external/libarchive_null.c"
#include "external/libcurl_null.c"
#include "external/tinyjson_null.c"
#include "formats/editorconfig.c"
#include "formats/ini.c"
#include "formats/lines.c"
#include "formats/tap.c"
#include "functions/array.c"
#include "functions/boolean.c"
#include "functions/both_libs.c"
#include "functions/build_target.c"
#include "functions/compiler.c"
#include "functions/configuration_data.c"
#include "functions/custom_target.c"
#include "functions/dependency.c"
#include "functions/dict.c"
#include "functions/disabler.c"
#include "functions/environment.c"
#include "functions/external_program.c"
#include "functions/feature_opt.c"
#include "functions/file.c"
#include "functions/generator.c"
#include "functions/kernel.c"
#include "functions/kernel/build_target.c"
#include "functions/kernel/configure_file.c"
#include "functions/kernel/custom_target.c"
#include "functions/kernel/dependency.c"
#include "functions/kernel/install.c"
#include "functions/kernel/options.c"
#include "functions/kernel/subproject.c"
#include "functions/machine.c"
#include "functions/meson.c"
#include "functions/modules.c"
#include "functions/modules/fs.c"
#include "functions/modules/keyval.c"
#include "functions/modules/pkgconfig.c"
#include "functions/modules/python.c"
#include "functions/modules/sourceset.c"
#include "functions/number.c"
#include "functions/run_result.c"
#include "functions/source_configuration.c"
#include "functions/source_set.c"
#include "functions/string.c"
#include "functions/subproject.c"
#include "guess.c"
#include "install.c"
#include "lang/analyze.c"
#include "lang/compiler.c"
#include "lang/eval.c"
#include "lang/fmt.c"
#include "lang/func_lookup.c"
#include "lang/lexer.c"
#include "lang/object.c"
#include "lang/object_iterators.c"
#include "lang/parser.c"
#include "lang/serial.c"
#include "lang/string.c"
#include "lang/typecheck.c"
#include "lang/vm.c"
#include "lang/workspace.c"
#include "log.c"
#include "machine_file.c"
#include "machines.c"
#include "main.c"
#include "memmem.c"
#include "meson_opts.c"
#include "options.c"
#include "opts.c"
#include "platform/assert.c"
#include "platform/filesystem.c"
#include "platform/mem.c"
#include "platform/os.c"
#include "platform/path.c"
#include "platform/run_cmd.c"
#include "platform/uname.c"
#include "rpmvercmp.c"
#include "sha_256.c"
#include "version.c.in"
#include "wrap.c"

#ifdef _WIN32
#include "platform/windows/filesystem.c"
#include "platform/windows/init.c"
#include "platform/windows/log.c"
#include "platform/windows/os.c"
#include "platform/windows/path.c"
#include "platform/windows/rpath_fixer.c"
#include "platform/windows/run_cmd.c"
#include "platform/windows/term.c"
#include "platform/windows/timer.c"
#include "platform/windows/uname.c"
#include "platform/windows/win32_error.c"
#else
#include "platform/null/rpath_fixer.c"
#include "platform/posix/filesystem.c"
#include "platform/posix/init.c"
#include "platform/posix/log.c"
#include "platform/posix/os.c"
#include "platform/posix/path.c"
#include "platform/posix/run_cmd.c"
#include "platform/posix/term.c"
#include "platform/posix/timer.c"
#include "platform/posix/uname.c"
#endif

#ifdef BOOTSTRAP_HAVE_LIBPKGCONF
#include "external/libpkgconf.c"
#else
#include "external/libpkgconf_null.c"
#endif

#ifdef BOOTSTRAP_NO_SAMU
#include "external/samurai_null.c"
#else
#include "external/samurai.c"
#include "external/samurai/build.c"
#include "external/samurai/deps.c"
#include "external/samurai/env.c"
#include "external/samurai/graph.c"
#include "external/samurai/htab.c"
#include "external/samurai/log.c"
#include "external/samurai/parse.c"
#include "external/samurai/samu.c"
#include "external/samurai/scan.c"
#include "external/samurai/tool.c"
#include "external/samurai/tree.c"
#include "external/samurai/util.c"
#endif
