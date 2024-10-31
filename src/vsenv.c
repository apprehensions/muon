/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>

#include "buf_size.h"
#include "lang/string.h"
#include "log.h"
#include "machines.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "vsenv.h"

bool
vsenv_setup(bool force)
{
	if (!force) {
		if (getenv("VSINSTALLDIR")) {
			return true;
		} else if (fs_has_cmd("cl.exe")) {
			return true;
		}
	}

	bool res = false;
	char tmp_path[512] = { 0 };
	SBUF_manual(path);
	SBUF_manual(ver);
	const char *program_files;
	struct run_cmd_ctx vswhere_cmd_ctx = { 0 }, bat_cmd_ctx = { 0 };
	uint32_t i;

	if (!(program_files = getenv("ProgramFiles(x86)"))) {
		LOG_E("vsenv: unable to get value of 'ProgramFiles(x86)' env var");
		goto ret;
	}

	path_join(0, &path, program_files, "Microsoft Visual Studio/Installer/vswhere.exe");

	if (!fs_file_exists(path.buf)) {
		LOG_E("vsenv: vswhere.exe not found @ %s", path.buf);
		goto ret;
	}

	if (!run_cmd_argv(&vswhere_cmd_ctx,
		    (char *const[]){
			    path.buf,
			    "-latest",
			    "-prerelease",
			    "-requiresAny",
			    "-requires",
			    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
			    "-requires",
			    "Microsoft.VisualStudio.Workload.WDExpress",
			    "-products",
			    "*",
			    "-utf8",
			    0,
		    },
		    0,
		    0)) {
		LOG_E("vsenv: failed to execute vswhere");
		goto ret;
	}

	if (vswhere_cmd_ctx.status != 0) {
		LOG_E("vsenv: exited with error status %d", vswhere_cmd_ctx.status);
		goto ret;
	}

	sbuf_clear(&path);

	struct str installation_path = { 0 }, installation_name = { 0 };
	struct {
		struct str label, *dest;
	} keys[] = {
		{ WKSTR("installationPath: "), &installation_path },
		{ WKSTR("installationName: "), &installation_name },
	};
	for (i = 0; i < vswhere_cmd_ctx.out.len;) {
		const char *line = vswhere_cmd_ctx.out.buf + i, *p = strstr(line, "\r\n");
		if (!p) {
			break;
		}
		uint32_t line_len = p - line;
		struct str l = { line, line_len };

		for (uint32_t i = 0; i < ARRAY_LEN(keys); ++i) {
			if (str_startswith(&l, &keys[i].label)) {
				l.s += keys[i].label.len;
				l.len -= keys[i].label.len;
				*keys[i].dest = l;
				break;
			}
		}

		if (installation_name.len && installation_path.len) {
			break;
		}

		i += line_len + 2;
	}

	if (!(installation_name.len && installation_path.len)) {
		LOG_E("vsenv: failed to parse vswhere output");
		goto ret;
	}

	sbuf_clear(&path);
	sbuf_pushn(0, &path, installation_path.s, installation_path.len);

	if (strcmp(build_machine.cpu, "arm64") == 0) {
		path_push(0, &path, "VC/Auxiliary/Build/vcvarsarm64.bat");
		if (!fs_file_exists(path.buf)) {
			sbuf_clear(&path);
			sbuf_pushn(0, &path, installation_path.s, installation_path.len);
			path_push(0, &path, "VC/Auxiliary/Build/vcvarsx86_arm64.bat");
		}
	} else {
		path_push(0, &path, "VC/Auxiliary/Build/vcvars64.bat");
		// if VS is not found try VS Express
		if (!fs_file_exists(path.buf)) {
			sbuf_clear(&path);
			sbuf_pushn(0, &path, installation_path.s, installation_path.len);
			path_push(0, &path, "VC/Auxiliary/Build/vcvarsx86_amd64.bat");
		}
	}

	if (!fs_file_exists(path.buf)) {
		LOG_E("vsenv: failed to locate vcvars @ %s", path.buf);
		goto ret;
	}

	L("vsenv: loading %.*s @ %s", installation_name.len, installation_name.s, path.buf);

	FILE *tmp = fs_make_tmp_file("vsenv_activate", ".bat", tmp_path, sizeof(tmp_path));
	if (!tmp) {
		LOG_E("vsenv: failed to create temporary file");
		goto ret;
	}

	const char *sep = "---SPLIT---";

	fprintf(tmp,
		"@ECHO OFF\r\n"
		"call \"%s\"\r\n"
		"ECHO %s\r\n"
		"SET\r\n",
		path.buf,
		sep);
	fs_fclose(tmp);

	if (!run_cmd_argv(&bat_cmd_ctx, (char *const[]){ tmp_path, 0 }, 0, 0)) {
		LOG_E("vsenv: failed to execute %s", tmp_path);
		goto ret;
	}

	bool sep_seen = false;
	for (i = 0; i < bat_cmd_ctx.out.len;) {
		const char *line = bat_cmd_ctx.out.buf + i, *p = strstr(line, "\r\n");
		if (!p) {
			break;
		}
		uint32_t line_len = p - line;
		struct str l = { line, line_len };

		if (!sep_seen) {
			if (str_eql(&l, &WKSTR(sep))) {
				sep_seen = true;
			}
		} else if ((p = memchr(l.s, '=', l.len))) {
				sbuf_clear(&path);
				sbuf_pushn(0, &path, l.s, l.len);
				sbuf_push(0, &path, 0);
				putenv(path.buf);
		}

		i += line_len + 2;
	}

	res = true;
ret:
	sbuf_destroy(&path);
	sbuf_destroy(&ver);
	run_cmd_ctx_destroy(&vswhere_cmd_ctx);
	run_cmd_ctx_destroy(&bat_cmd_ctx);
	if (*tmp_path) {
		fs_remove(tmp_path);
	}
	return res;
}
