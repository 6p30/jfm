#include "jfm.h"
#include "xdg.h"

#include <pwd.h>

struct Config {
	char *file;
	char **keys;
	char **vals;
	int n;
	int cap;
};

static char *
xdg_env(const char *name, const char *fallback)
{
	const char *e = getenv(name);
	if (e && *e == '/')
		return strdup(e);
	return strdup(fallback);
}

char *
xdg_home(void)
{
	const char *e = getenv("HOME");
	if (e && *e)
		return strdup(e);
	struct passwd *pw = getpwuid(getuid());
	if (pw && pw->pw_dir && *pw->pw_dir)
		return strdup(pw->pw_dir);
	return strdup("/");
}

char *
xdg_config_home(void)
{
	char *home = xdg_home();
	char *p = smprint("%s/.config", home);
	free(home);
	return xdg_env("XDG_CONFIG_HOME", p);
}

char *
xdg_data_home(void)
{
	char *home = xdg_home();
	char *p = smprint("%s/.local/share", home);
	free(home);
	return xdg_env("XDG_DATA_HOME", p);
}

char *
xdg_cache_home(void)
{
	char *home = xdg_home();
	char *p = smprint("%s/.cache", home);
	free(home);
	return xdg_env("XDG_CACHE_HOME", p);
}

char *
xdg_state_home(void)
{
	char *home = xdg_home();
	char *p = smprint("%s/.local/state", home);
	free(home);
	return xdg_env("XDG_STATE_HOME", p);
}

char *
xdg_runtime_dir(void)
{
	const char *e = getenv("XDG_RUNTIME_DIR");
	if (e && *e == '/')
		return strdup(e);
	char *p = smprint("/tmp/runtime-%d", (int)getuid());
	xdg_mkdirp(p, 0700);
	return p;
}

char *
xdg_config_dirs(void)
{
	return xdg_env("XDG_CONFIG_DIRS", "/etc/xdg");
}

char *
xdg_data_dirs(void)
{
	return xdg_env("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
}

char *
xdg_path_join(const char *base, const char *rel)
{
	if (!rel || !*rel)
		return strdup(base ? base : "");
	if (rel[0] == '/')
		return strdup(rel);
	size_t bl = strlen(base);
	while (bl > 1 && base[bl - 1] == '/')
		bl--;
	return smprint("%.*s/%s", (int)bl, base, rel);
}

char *
xdg_config_path(const char *rel)
{
	char *base = xdg_config_home();
	char *p = xdg_path_join(base, rel);
	free(base);
	return p;
}

char *
xdg_data_path(const char *rel)
{
	char *base = xdg_data_home();
	char *p = xdg_path_join(base, rel);
	free(base);
	return p;
}

char *
xdg_cache_path(const char *rel)
{
	char *base = xdg_cache_home();
	char *p = xdg_path_join(base, rel);
	free(base);
	return p;
}

char *
xdg_state_path(const char *rel)
{
	char *base = xdg_state_home();
	char *p = xdg_path_join(base, rel);
	free(base);
	return p;
}

int
xdg_mkdirp(const char *path, mode_t mode)
{
	char *copy = strdup(path);
	size_t len = strlen(copy);
	while (len > 1 && copy[len - 1] == '/')
		copy[--len] = 0;
	int rc = 0;
	for (char *p = copy + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(copy, mode) < 0 && errno != EEXIST)
				rc = -1;
			*p = '/';
		}
	}
	if (rc == 0 && mkdir(copy, mode) < 0 && errno != EEXIST)
		rc = -1;
	free(copy);
	return rc;
}

int
xdg_mkdir_parents(const char *path)
{
	char *copy = strdup(path);
	char *slash = strrchr(copy, '/');
	if (!slash) {
		free(copy);
		return 0;
	}
	if (slash == copy)
		slash[1] = 0;
	else
		*slash = 0;
	int rc = xdg_mkdirp(copy, 0755);
	free(copy);
	return rc;
}

char *
xdg_user_dir(const char *name)
{
	char *cfg = xdg_config_home();
	char *file = xdg_path_join(cfg, "user-dirs.dirs");
	free(cfg);
	FILE *f = fopen(file, "r");
	free(file);
	if (!f)
		return nil;
	char key[128];
	snprintf(key, sizeof(key), "XDG_%s_DIR", name);
	char *home = xdg_home();
	char *result = nil;
	char line[2048];
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, key, strlen(key)) != 0)
			continue;
		p += strlen(key);
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p != '=')
			continue;
		p++;
		while (*p == ' ' || *p == '\t')
			p++;
		char *q = p;
		char *end = nil;
		if (*q == '"')
			end = strchr(q + 1, '"');
		else
			end = strchr(q, '\n');
		if (!end)
			continue;
		*end = 0;
		char *val = q + (q[0] == '"' ? 1 : 0);
		if (strncmp(val, "$HOME", 5) == 0)
			result = smprint("%s%s", home, val + 5);
		else
			result = strdup(val);
		break;
	}
	fclose(f);
	free(home);
	return result;
}

static const struct {
	const char *ext;
	const char *mime;
} mimetab[] = {
	{ "txt", "text/plain" },
	{ "md", "text/markdown" },
	{ "log", "text/plain" },
	{ "conf", "text/plain" },
	{ "cfg", "text/plain" },
	{ "ini", "text/plain" },
	{ "c", "text/x-c" },
	{ "h", "text/x-c" },
	{ "py", "text/x-python" },
	{ "sh", "application/x-shellscript" },
	{ "json", "application/json" },
	{ "xml", "application/xml" },
	{ "html", "text/html" },
	{ "htm", "text/html" },
	{ "css", "text/css" },
	{ "js", "text/javascript" },
	{ "pdf", "application/pdf" },
	{ "png", "image/png" },
	{ "jpg", "image/jpeg" },
	{ "jpeg", "image/jpeg" },
	{ "gif", "image/gif" },
	{ "bmp", "image/bmp" },
	{ "webp", "image/webp" },
	{ "svg", "image/svg+xml" },
	{ "ico", "image/vnd.microsoft.icon" },
	{ "mp3", "audio/mpeg" },
	{ "ogg", "audio/ogg" },
	{ "wav", "audio/x-wav" },
	{ "flac", "audio/flac" },
	{ "mp4", "video/mp4" },
	{ "mkv", "video/x-matroska" },
	{ "webm", "video/webm" },
	{ "avi", "video/x-msvideo" },
	{ "zip", "application/zip" },
	{ "tar", "application/x-tar" },
	{ "gz", "application/gzip" },
	{ "bz2", "application/x-bzip2" },
	{ "xz", "application/x-xz" },
	{ "7z", "application/x-7z-compressed" },
	{ "deb", "application/vnd.debian.binary-package" },
	{ "rpm", "application/x-rpm" },
	{ "so", "application/x-shared-obj" },
	{ "o", "application/x-object" },
	{ "desktop", "application/x-desktop" },
	{ "patch", "text/x-patch" },
	{ "diff", "text/x-patch" },
	{ "lock", "text/plain" },
	{ "list", "text/plain" },
	{ "1", "text/plain" },
	{ "2", "text/plain" },
};

static char *xdg_mime_of1(const char *path, int depth);

char *
xdg_mime_of(const char *path)
{
	return xdg_mime_of1(path, 0);
}

static char *
xdg_mime_of1(const char *path, int depth)
{
	struct stat st;
	if (lstat(path, &st) < 0)
		return strdup("application/octet-stream");
	if (S_ISLNK(st.st_mode) && depth < 16) {
		char buf[4096];
		ssize_t n = readlink(path, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = 0;
			char *joined;
			if (buf[0] == '/') {
				joined = strdup(buf);
			} else {
				/* resolve a relative link target against the link's directory */
				const char *slash = strrchr(path, '/');
				if (slash)
					joined = smprint("%.*s/%s", (int)(slash - path), path, buf);
				else
					joined = strdup(buf);
			}
			char *m = xdg_mime_of1(joined, depth + 1);
			free(joined);
			return m;
		}
	}
	if (S_ISDIR(st.st_mode))
		return strdup("inode/directory");
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	const char *dot = strrchr(base, '.');
	if (dot && dot[1]) {
		for (size_t i = 0; i < sizeof(mimetab) / sizeof(mimetab[0]); i++) {
			if (strcmp(dot + 1, mimetab[i].ext) == 0)
				return strdup(mimetab[i].mime);
		}
	}
	FILE *f = fopen(path, "rb");
	if (!f)
		return strdup("application/octet-stream");
	char buf[1024];
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n == 0)
		return strdup("application/x-zerosize");
	if (buf[0] == '#' && buf[1] == '!')
		return strdup("application/x-shellscript");
	for (size_t i = 0; i < n; i++) {
		if (buf[i] == '\0')
			return strdup("application/octet-stream");
	}
	return strdup("text/plain");
}

static char **
xdg_list_dirs(const char *list, int *n)
{
	char **dirs = nil;
	int count = 0, cap = 0;
	char *copy = strdup(list);
	char *save = nil;
	for (char *tok = strtok_r(copy, ":", &save); tok; tok = strtok_r(nil, ":", &save)) {
		if (!*tok)
			continue;
		if (count == cap) {
			cap = cap ? cap * 2 : 8;
			dirs = realloc(dirs, sizeof(char *) * cap);
		}
		dirs[count++] = strdup(tok);
	}
	free(copy);
	*n = count;
	return dirs;
}

static char **
xdg_mimeapps_files(int *n)
{
	char **files = nil;
	int count = 0, cap = 0;
	char *cfg = xdg_config_home();
	char *cfgdirs = xdg_config_dirs();
	char *datahome = xdg_data_home();
	char *datadirs = xdg_data_dirs();

	char *lists[4];
	lists[0] = xdg_path_join(cfg, "mimeapps.list");
	lists[1] = nil;
	lists[2] = xdg_path_join(datahome, "applications/mimeapps.list");
	lists[3] = nil;

	int ncfg, ndata;
	char **cdirs = xdg_list_dirs(cfgdirs, &ncfg);
	char **ddirs = xdg_list_dirs(datadirs, &ndata);

	char *all[64];
	int nall = 0;
	all[nall++] = lists[0];
	for (int i = 0; i < ncfg && nall < 60; i++)
		all[nall++] = xdg_path_join(cdirs[i], "mimeapps.list");
	all[nall++] = lists[2];
	for (int i = 0; i < ndata && nall < 62; i++)
		all[nall++] = xdg_path_join(ddirs[i], "applications/mimeapps.list");

	for (int i = 0; i < nall; i++) {
		if (count == cap) {
			cap = cap ? cap * 2 : 8;
			files = realloc(files, sizeof(char *) * cap);
		}
		files[count++] = all[i];
	}

	for (int i = 0; i < ncfg; i++)
		free(cdirs[i]);
	free(cdirs);
	for (int i = 0; i < ndata; i++)
		free(ddirs[i]);
	free(ddirs);
	free(cfg);
	free(cfgdirs);
	free(datahome);
	free(datadirs);

	*n = count;
	return files;
}

static char *
xdg_trim_copy(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t'))
		len--;
	char *out = malloc(len + 1);
	memcpy(out, s, len);
	out[len] = 0;
	return out;
}

static char *
xdg_mimeapps_lookup(const char *mime, const char *section)
{
	int nfiles = 0;
	char **files = xdg_mimeapps_files(&nfiles);
	char *result = nil;
	for (int i = 0; i < nfiles && !result; i++) {
		FILE *f = fopen(files[i], "r");
		if (!f)
			continue;
		char line[4096];
		int insection = 0;
		while (fgets(line, sizeof(line), f)) {
			char *p = line;
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == '[') {
				char *end = strchr(p, ']');
				if (end) {
					*end = 0;
					insection = strcmp(p + 1, section) == 0;
				}
				continue;
			}
			if (!insection)
				continue;
			size_t klen = strlen(mime);
			if (strncmp(p, mime, klen) != 0 || p[klen] != '=')
				continue;
			char *val = xdg_trim_copy(p + klen + 1);
			char *semi = strchr(val, ';');
			if (semi)
				*semi = 0;
			char *tok = val;
			while (tok && *tok) {
				while (*tok == ' ')
					tok++;
				char *sp = strchr(tok, ' ');
				if (sp)
					*sp = 0;
				if (*tok) {
					char *df = xdg_desktop_file(tok);
					if (df) {
						free(df);
						result = strdup(tok);
						break;
					}
				}
				tok = sp ? sp + 1 : nil;
			}
			free(val);
			if (result)
				break;
		}
		fclose(f);
	}
	for (int i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);
	return result;
}

char *
xdg_mime_default(const char *mime)
{
	char *id = xdg_mimeapps_lookup(mime, "Default Applications");
	if (id)
		return id;
	return xdg_mimeapps_lookup(mime, "Deprecated");
}

static void
xdg_collect_apps(char ***out, int *count, const char *id)
{
	for (int j = 0; j < *count; j++)
		if (strcmp((*out)[j], id) == 0)
			return;
	*out = realloc(*out, sizeof(char *) * (*count + 1));
	(*out)[(*count)++] = strdup(id);
}

static void
xdg_scan_desktop_mime(char ***out, int *count, const char *dir, const char *mime)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d))) {
		const char *name = de->d_name;
		size_t len = strlen(name);
		if (len < 8 || strcmp(name + len - 8, ".desktop") != 0)
			continue;
		char *full = xdg_path_join(dir, name);
		FILE *f = fopen(full, "r");
		if (!f) {
			free(full);
			continue;
		}
		int inentry = 0, match = 0;
		char line[8192];
		while (fgets(line, sizeof(line), f)) {
			char *p = line;
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == '[') {
				char *end = strchr(p, ']');
				if (end) {
					*end = 0;
					inentry = strcmp(p + 1, "Desktop Entry") == 0;
				}
				continue;
			}
			if (!inentry)
				continue;
			if (strncmp(p, "MimeType=", 9) != 0)
				continue;
			char *list = p + 9;
			char *nl = strchr(list, '\n');
			if (nl)
				*nl = 0;
			char *copy = strdup(list);
			char *save = nil;
			for (char *tok = strtok_r(copy, ";", &save); tok; tok = strtok_r(nil, ";", &save)) {
				if (strcmp(tok, mime) == 0) {
					match = 1;
					break;
				}
			}
			free(copy);
			if (match)
				break;
		}
		fclose(f);
		if (match)
			xdg_collect_apps(out, count, name);
		free(full);
	}
	closedir(d);
}

char **
xdg_mime_apps(const char *mime, int *n)
{
	char **out = nil;
	int count = 0;
	int nfiles = 0;
	char **files = xdg_mimeapps_files(&nfiles);
	const char *sections[] = { "Default Applications", "Added Associations" };
	for (int s = 0; s < 2; s++) {
		for (int i = 0; i < nfiles; i++) {
			FILE *f = fopen(files[i], "r");
			if (!f)
				continue;
			char line[4096];
			int insection = 0;
			while (fgets(line, sizeof(line), f)) {
				char *p = line;
				while (*p == ' ' || *p == '\t')
					p++;
				if (*p == '[') {
					char *end = strchr(p, ']');
					if (end) {
						*end = 0;
						insection = strcmp(p + 1, sections[s]) == 0;
					}
					continue;
				}
				if (!insection)
					continue;
				size_t klen = strlen(mime);
				if (strncmp(p, mime, klen) != 0 || p[klen] != '=')
					continue;
				char *val = xdg_trim_copy(p + klen + 1);
				char *semi = strchr(val, ';');
				if (semi)
					*semi = 0;
				char *tok = val;
				while (tok && *tok) {
					while (*tok == ' ')
						tok++;
					char *sp = strchr(tok, ' ');
					if (sp)
						*sp = 0;
					if (*tok) {
						char *df = xdg_desktop_file(tok);
						if (df) {
							free(df);
							xdg_collect_apps(&out, &count, tok);
						}
					}
					tok = sp ? sp + 1 : nil;
				}
				free(val);
			}
			fclose(f);
		}
	}
	for (int i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);

	char *datahome = xdg_data_home();
	char *datadirs = xdg_data_dirs();
	int ndata = 0;
	char **ddirs = xdg_list_dirs(datadirs, &ndata);
	char *homeapps = xdg_path_join(datahome, "applications");
	xdg_scan_desktop_mime(&out, &count, homeapps, mime);
	free(homeapps);
	for (int i = 0; i < ndata; i++) {
		char *base = xdg_path_join(ddirs[i], "applications");
		xdg_scan_desktop_mime(&out, &count, base, mime);
		free(base);
	}
	for (int i = 0; i < ndata; i++)
		free(ddirs[i]);
	free(ddirs);
	free(datadirs);
	free(datahome);

	*n = count;
	return out;
}

char *
xdg_desktop_file(const char *desktopid)
{
	if (!desktopid || !*desktopid)
		return nil;
	char *name = strdup(desktopid);
	if (!strstr(name, ".desktop")) {
		char *tmp = smprint("%s.desktop", name);
		free(name);
		name = tmp;
	}
	char *datahome = xdg_data_home();
	char *datadirs = xdg_data_dirs();
	int ndata = 0;
	char **ddirs = xdg_list_dirs(datadirs, &ndata);
	char *result = nil;
	char *base = xdg_path_join(datahome, "applications");
	char *p = xdg_path_join(base, name);
	free(base);
	if (access(p, F_OK) == 0)
		result = strdup(p);
	free(p);
	for (int i = 0; i < ndata && !result; i++) {
		char *base = xdg_path_join(ddirs[i], "applications");
		char *cand = xdg_path_join(base, name);
		if (access(cand, F_OK) == 0)
			result = strdup(cand);
		free(cand);
		free(base);
	}
	for (int i = 0; i < ndata; i++)
		free(ddirs[i]);
	free(ddirs);
	free(datadirs);
	free(datahome);
	free(name);
	return result;
}

char *
xdg_desktop_field(const char *desktopid, const char *field)
{
	char *file = xdg_desktop_file(desktopid);
	if (!file)
		return nil;
	FILE *f = fopen(file, "r");
	free(file);
	if (!f)
		return nil;
	char *result = nil;
	char line[4096];
	int inentry = 0;
	size_t flen = strlen(field);
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				*end = 0;
				inentry = strcmp(p + 1, "Desktop Entry") == 0;
			}
			continue;
		}
		if (!inentry)
			continue;
		if (strncmp(p, field, flen) != 0 || p[flen] != '=')
			continue;
		char *val = p + flen + 1;
		char *nl = strchr(val, '\n');
		if (nl)
			*nl = 0;
		nl = strchr(val, '\r');
		if (nl)
			*nl = 0;
		result = strdup(val);
		break;
	}
	fclose(f);
	return result;
}

char *
xdg_desktop_exec(const char *desktopid)
{
	return xdg_desktop_field(desktopid, "Exec");
}

static char *
xdg_desktop_exec_expanded(const char *desktopid)
{
	char *exec = xdg_desktop_exec(desktopid);
	if (!exec)
		return nil;
	char *icon = xdg_desktop_field(desktopid, "Icon");
	char *name = xdg_desktop_field(desktopid, "Name");
	char *file = xdg_desktop_file(desktopid);
	char *out = nil;
	size_t cap = strlen(exec) + 1024;
	out = malloc(cap);
	out[0] = 0;
	for (size_t i = 0; exec[i];) {
		if (exec[i] != '%') {
			size_t l = strlen(out);
			if (l + 2 >= cap) {
				cap *= 2;
				out = realloc(out, cap);
			}
			out[l] = exec[i];
			out[l + 1] = 0;
			i++;
			continue;
		}
		char c = exec[i + 1];
		const char *rep = nil;
		char *tmp = nil;
		switch (c) {
		case '%':
			rep = "%";
			break;
		case 'i':
			if (icon) {
				tmp = smprint(" --icon %s", icon);
				rep = tmp;
			}
			break;
		case 'c':
			if (name) {
				tmp = smprint("'%s'", name);
				rep = tmp;
			}
			break;
		case 'k':
			if (file) {
				tmp = smprint("'%s'", file);
				rep = tmp;
			}
			break;
		default:
			break;
		}
		if (rep) {
			size_t l = strlen(out);
			size_t rl = strlen(rep);
			while (l + rl + 2 >= cap) {
				cap *= 2;
				out = realloc(out, cap);
			}
			memcpy(out + l, rep, rl);
			out[l + rl] = 0;
			free(tmp);
		}
		i += 2;
	}
	free(exec);
	free(icon);
	free(name);
	free(file);
	return out;
}

int
xdg_run_desktop(const char *desktopid, const char *arg)
{
	char *exec = xdg_desktop_exec_expanded(desktopid);
	if (!exec)
		return -1;
	char *uri = nil;
	char *quoted = nil;
	if (arg) {
		uri = xdg_path_to_uri(arg);
		quoted = quotestrdup(arg);
	}
	char *quoteduri = uri ? quotestrdup(uri) : nil;
	char *cmd = nil;
	size_t cap = strlen(exec) + 1024;
	cmd = malloc(cap);
	cmd[0] = 0;
	for (size_t i = 0; exec[i];) {
		if (exec[i] == '%' && exec[i + 1]) {
			char c = exec[i + 1];
			const char *rep = nil;
			if ((c == 'f' || c == 'F') && quoted)
				rep = quoted;
			else if ((c == 'u' || c == 'U') && quoteduri)
				rep = quoteduri;
			if (rep) {
				size_t l = strlen(cmd);
				size_t rl = strlen(rep);
				while (l + rl + 2 >= cap) {
					cap *= 2;
					cmd = realloc(cmd, cap);
				}
				memcpy(cmd + l, rep, rl);
				cmd[l + rl] = 0;
			}
			i += 2;
			continue;
		}
		size_t l = strlen(cmd);
		while (l + 2 >= cap) {
			cap *= 2;
			cmd = realloc(cmd, cap);
		}
		cmd[l] = exec[i];
		cmd[l + 1] = 0;
		i++;
	}
	free(exec);
	free(uri);
	free(quoted);
	free(quoteduri);
	int rc = system(cmd);
	free(cmd);
	return rc;
}

int
xdg_open_path(const char *path)
{
	char *mime = xdg_mime_of(path);
	char *app = xdg_mime_default(mime);
	int rc = -1;
	if (app) {
		rc = xdg_run_desktop(app, path);
		free(app);
	}
	free(mime);
	if (rc != 0) {
		char *q = quotestrdup(path);
		char *cmd = smprint("xdg-open %s >/dev/null 2>&1", q);
		rc = system(cmd);
		free(cmd);
		free(q);
	}
	return rc;
}

int
xdg_open_uri(const char *uri)
{
	char *q = quotestrdup(uri);
	char *cmd = smprint("xdg-open %s >/dev/null 2>&1", q);
	int rc = system(cmd);
	free(cmd);
	free(q);
	return rc;
}

char *
xdg_path_to_uri(const char *path)
{
	if (!path)
		return nil;
	if (strncmp(path, "file://", 7) == 0)
		return strdup(path);
	if (path[0] != '/') {
		/* relative path: resolve against the current directory first */
		char cwd[4096];
		if (!getcwd(cwd, sizeof(cwd)))
			return nil;
		char *abs = smprint("%s/%s", cwd, path);
		char *uri = xdg_path_to_uri(abs);
		free(abs);
		return uri;
	}
	size_t len = strlen(path);
	char *out = malloc(len * 3 + 16);
	size_t o = 0;
	out[o++] = 'f';
	out[o++] = 'i';
	out[o++] = 'l';
	out[o++] = 'e';
	out[o++] = ':';
	out[o++] = '/';
	out[o++] = '/';
	for (size_t i = 0; path[i]; i++) {
		unsigned char c = path[i];
		if (i == 0 && c != '/')
			out[o++] = '/';
		if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') {
			out[o++] = c;
		} else {
			static const char hex[] = "0123456789ABCDEF";
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 0xf];
		}
	}
	out[o] = 0;
	return out;
}

static int
xdg_hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

char *
xdg_uri_to_path(const char *uri)
{
	if (!uri)
		return nil;
	if (strncmp(uri, "file://", 7) != 0)
		return strdup(uri);
	const char *p = uri + 7;
	const char *slash = strchr(p, '/');
	if (!slash)
		return strdup("/");
	p = slash;
	size_t len = strlen(p);
	char *out = malloc(len + 1);
	size_t o = 0;
	for (size_t i = 0; p[i]; i++) {
		if (p[i] == '%' && p[i + 1] && p[i + 2]) {
			int h = xdg_hexval(p[i + 1]);
			int l = xdg_hexval(p[i + 2]);
			if (h >= 0 && l >= 0) {
				out[o++] = (char)((h << 4) | l);
				i += 2;
				continue;
			}
		}
		out[o++] = p[i];
	}
	out[o] = 0;
	return out;
}

static char **
xdg_read_lines(const char *file, int *n)
{
	char **lines = nil;
	int count = 0, cap = 0;
	FILE *f = fopen(file, "r");
	if (!f) {
		*n = 0;
		return nil;
	}
	char line[8192];
	while (fgets(line, sizeof(line), f)) {
		if (count == cap) {
			cap = cap ? cap * 2 : 32;
			lines = realloc(lines, sizeof(char *) * cap);
		}
		lines[count++] = strdup(line);
	}
	fclose(f);
	*n = count;
	return lines;
}

static int
xdg_write_lines(const char *file, char **lines, int n)
{
	xdg_mkdir_parents(file);
	FILE *f = fopen(file, "w");
	if (!f)
		return -1;
	for (int i = 0; i < n; i++)
		fputs(lines[i], f);
	fclose(f);
	return 0;
}

static int
xdg_mimeapps_edit(const char *mime, const char *desktopid, int remove)
{
	char *file = xdg_config_path("mimeapps.list");
	int n = 0;
	char **lines = xdg_read_lines(file, &n);
	int found = 0;
	int section = -1;
	int next_section = -1;
	size_t mlen = strlen(mime);
	char *newline = smprint("%s=%s\n", mime, desktopid ? desktopid : "");
	int changed = 0;
	for (int i = 0; i < n; i++) {
		char *p = lines[i];
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				char name[256];
				size_t len = end - p - 1;
				if (len >= sizeof(name))
					len = sizeof(name) - 1;
				memcpy(name, p + 1, len);
				name[len] = 0;
				if (strcmp(name, "Default Applications") == 0)
					section = i;
				else if (section >= 0 && next_section < 0)
					next_section = i;
			}
			continue;
		}
		if (section < 0 || (next_section >= 0 && i >= next_section))
			continue;
		if (strncmp(p, mime, mlen) == 0 && p[mlen] == '=') {
			if (remove) {
				free(lines[i]);
				lines[i] = strdup("");
			} else {
				free(lines[i]);
				lines[i] = strdup(newline);
			}
			found = 1;
			changed = 1;
		}
	}
	if (!remove && !found) {
		char **nl = realloc(lines, sizeof(char *) * (n + 2));
		lines = nl;
		if (section < 0) {
			lines[n++] = strdup("[Default Applications]\n");
			lines[n++] = strdup(newline);
		} else {
			int ins = (next_section >= 0) ? next_section : n;
			for (int j = n; j > ins; j--)
				lines[j] = lines[j - 1];
			lines[ins] = strdup(newline);
			n++;
		}
		changed = 1;
	}
	free(newline);
	if (changed)
		xdg_write_lines(file, lines, n);
	for (int i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	free(file);
	return changed ? 0 : -1;
}

int
xdg_mimeapps_set_default(const char *mime, const char *desktopid)
{
	return xdg_mimeapps_edit(mime, desktopid, 0);
}

int
xdg_mimeapps_remove_default(const char *mime, const char *desktopid)
{
	return xdg_mimeapps_edit(mime, desktopid, 1);
}

int
xdg_mimeapps_has_default(const char *mime, const char *desktopid)
{
	char *id = xdg_mime_default(mime);
	if (!id)
		return 0;
	int rc = strcmp(id, desktopid) == 0;
	free(id);
	return rc;
}

Config *
config_new(const char *file)
{
	Config *c = calloc(1, sizeof(Config));
	c->file = strdup(file);
	return c;
}

void
config_free(Config *c)
{
	if (!c)
		return;
	for (int i = 0; i < c->n; i++) {
		free(c->keys[i]);
		free(c->vals[i]);
	}
	free(c->keys);
	free(c->vals);
	free(c->file);
	free(c);
}

int
config_has(Config *c, const char *key)
{
	for (int i = 0; i < c->n; i++)
		if (strcmp(c->keys[i], key) == 0)
			return 1;
	return 0;
}

const char *
config_get(Config *c, const char *key, const char *def)
{
	for (int i = 0; i < c->n; i++)
		if (strcmp(c->keys[i], key) == 0)
			return c->vals[i];
	return def;
}

int
config_set(Config *c, const char *key, const char *value)
{
	for (int i = 0; i < c->n; i++) {
		if (strcmp(c->keys[i], key) == 0) {
			free(c->vals[i]);
			c->vals[i] = strdup(value);
			return 0;
		}
	}
	if (c->n == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 16;
		c->keys = realloc(c->keys, sizeof(char *) * c->cap);
		c->vals = realloc(c->vals, sizeof(char *) * c->cap);
	}
	c->keys[c->n] = strdup(key);
	c->vals[c->n] = strdup(value);
	c->n++;
	return 0;
}

static char *
xdg_trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *end = s + strlen(s);
	while (end > s && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
		end--;
	*end = 0;
	return s;
}

int
config_save(Config *c)
{
	if (!c)
		return -1;
	xdg_mkdir_parents(c->file);
	FILE *f = fopen(c->file, "w");
	if (!f)
		return -1;
	fprintf(f, "[jfm]\n");
	for (int i = 0; i < c->n; i++)
		fprintf(f, "%s=%s\n", c->keys[i], c->vals[i]);
	fclose(f);
	return 0;
}

Config *
config_open(void)
{
	char *file = xdg_config_path("jfm/config");
	Config *c = config_new(file);
	free(file);
	FILE *f = fopen(c->file, "r");
	if (!f)
		return c;
	char line[4096];
	while (fgets(line, sizeof(line), f)) {
		char *p = xdg_trim(line);
		if (!*p || *p == '#' || *p == '[')
			continue;
		char *eq = strchr(p, '=');
		if (!eq)
			continue;
		*eq = 0;
		char *key = xdg_trim(p);
		char *val = xdg_trim(eq + 1);
		if (*key)
			config_set(c, key, val);
	}
	fclose(f);
	return c;
}

static const char desktop_template[] =
"[Desktop Entry]\n"
"Type=Application\n"
"Version=1.0\n"
"Name=jfm\n"
"GenericName=File Manager\n"
"Comment=Manage files\n"
"TryExec=@EXEC@\n"
"Exec=\"@EXEC@\" %U\n"
"Icon=@ICON@\n"
"Terminal=false\n"
"StartupNotify=true\n"
"Categories=System;FileManager;Filesystem;Utility;Core;\n"
"MimeType=inode/directory;\n"
"Actions=new-window;\n"
"X-JFM-Select-File=\"@EXEC@\" --select-file\n"
"\n"
"[Desktop Action new-window]\n"
"Name=New Window\n"
"Exec=@EXEC@\n";

char *
xdg_desktop_template(void)
{
	return strdup(desktop_template);
}

static char *
xdg_find_icon_source(const char *exe)
{
	char *copy = strdup(exe);
	char *slash = strrchr(copy, '/');
	char *dir = nil;
	if (slash) {
		*slash = 0;
		dir = strdup(copy);
	} else {
		dir = strdup(".");
	}
	free(copy);
	char *cands[4];
	cands[0] = xdg_path_join(dir, "jfm.png");
	cands[1] = xdg_path_join(dir, "../share/jfm/jfm.png");
	cands[2] = xdg_path_join(dir, "../share/icons/hicolor/256x256/apps/jfm.png");
	char *found = nil;
	for (int i = 0; i < 3 && !found; i++) {
		if (access(cands[i], F_OK) == 0)
			found = strdup(cands[i]);
	}
	for (int i = 0; i < 3; i++)
		free(cands[i]);
	free(dir);
	return found;
}

static char *
xdg_replace(char *text, const char *from, const char *to)
{
	size_t flen = strlen(from);
	size_t tlen = strlen(to);
	char *out = nil;
	size_t o = 0, cap = 0;
	const char *p = text;
	while (*p) {
		if (strncmp(p, from, flen) == 0) {
			while (o + tlen + 1 > cap) {
				cap = cap ? cap * 2 : 256;
				out = realloc(out, cap);
			}
			memcpy(out + o, to, tlen);
			o += tlen;
			p += flen;
			continue;
		}
		while (o + 2 > cap) {
			cap = cap ? cap * 2 : 256;
			out = realloc(out, cap);
		}
		out[o++] = *p++;
	}
	if (!out) {
		out = malloc(1);
		cap = 1;
	}
	out[o] = 0;
	return out;
}

int
xdg_install_desktop(const char *exe)
{
	char *appsdir = xdg_data_path("applications");
	xdg_mkdirp(appsdir, 0755);
	char *file = xdg_path_join(appsdir, "jfm.desktop");
	char *exec = strdup(exe);
	char *tmpl = xdg_desktop_template();
	char *content = xdg_replace(tmpl, "@EXEC@", exec);
	char *content2 = xdg_replace(content, "@ICON@", "jfm");
	free(content);
	content = content2;
	FILE *f = fopen(file, "w");
	if (!f) {
		free(appsdir);
		free(file);
		free(exec);
		free(tmpl);
		free(content);
		return -1;
	}
	fputs(content, f);
	fclose(f);
	free(content);
	free(tmpl);

	char *iconsrc = xdg_find_icon_source(exe);
	if (iconsrc) {
		char *icondir = xdg_data_path("icons/hicolor/256x256/apps");
		xdg_mkdirp(icondir, 0755);
		char *icondst = xdg_path_join(icondir, "jfm.png");
		FILE *src = fopen(iconsrc, "rb");
		if (src) {
			FILE *dst = fopen(icondst, "wb");
			if (dst) {
				char buf[8192];
				size_t n;
				while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
					fwrite(buf, 1, n, dst);
				fclose(dst);
			}
			fclose(src);
		}
		free(icondst);
		free(icondir);
		free(iconsrc);
	}

	char *servdir = xdg_data_path("dbus-1/services");
	xdg_mkdirp(servdir, 0755);
	char *sfile = xdg_path_join(servdir, "org.freedesktop.FileManager1.service");
	f = fopen(sfile, "w");
	if (f) {
		fprintf(f, "[D-BUS Service]\n");
		fprintf(f, "Name=org.freedesktop.FileManager1\n");
		fprintf(f, "Exec=%s\n", exec);
		fclose(f);
	}
	free(sfile);
	free(servdir);
	free(exec);

	free(file);
	char *cmd = smprint("update-desktop-database %s >/dev/null 2>&1", appsdir);
	if (system(cmd) != 0)
		;
	free(cmd);
	free(appsdir);
	return 0;
}

int
xdg_uninstall_desktop(void)
{
	char *appsdir = xdg_data_path("applications");
	char *file = xdg_path_join(appsdir, "jfm.desktop");
	unlink(file);
	free(file);
	char *cmd = smprint("update-desktop-database %s >/dev/null 2>&1", appsdir);
	if (system(cmd) != 0)
		;
	free(cmd);
	free(appsdir);

	char *icondir = xdg_data_path("icons/hicolor/256x256/apps");
	char *icon = xdg_path_join(icondir, "jfm.png");
	unlink(icon);
	free(icon);
	free(icondir);

	char *servdir = xdg_data_path("dbus-1/services");
	char *sfile = xdg_path_join(servdir, "org.freedesktop.FileManager1.service");
	unlink(sfile);
	free(sfile);
	free(servdir);

	xdg_mimeapps_remove_default("inode/directory", "jfm.desktop");
	return 0;
}

int
xdg_install_portal(const char *exe)
{
	char *dir = xdg_data_path("xdg-desktop-portal/portals");
	xdg_mkdirp(dir, 0755);
	char *file = xdg_path_join(dir, "jfm.portal");
	FILE *f = fopen(file, "w");
	if (f) {
		fprintf(f, "[portal]\n");
		fprintf(f, "DBusName=org.freedesktop.impl.portal.jfm\n");
		fprintf(f, "Interfaces=org.freedesktop.impl.portal.FileChooser;\n");
		fprintf(f, "UseIn=*\n");
		fclose(f);
	}
	free(file);
	free(dir);

	char *servdir = xdg_data_path("dbus-1/services");
	xdg_mkdirp(servdir, 0755);
	char *sfile = xdg_path_join(servdir, "org.freedesktop.impl.portal.jfm.service");
	f = fopen(sfile, "w");
	if (f) {
		fprintf(f, "[D-BUS Service]\n");
		fprintf(f, "Name=org.freedesktop.impl.portal.jfm\n");
		fprintf(f, "Exec=%s --portal-backend\n", exe);
		fclose(f);
	}
	free(sfile);
	free(servdir);

	char *pconf = xdg_config_path("xdg-desktop-portal/portals.conf");
	xdg_mkdir_parents(pconf);
	int nlines = 0;
	char **lines = xdg_read_lines(pconf, &nlines);
	int found_section = 0;
	int found_entry = 0;
	for (int i = 0; i < nlines; i++) {
		if (strncmp(lines[i], "[preferred]", 11) == 0)
			found_section = 1;
		if (strncmp(lines[i], "org.freedesktop.impl.portal.FileChooser=", 40) == 0) {
			free(lines[i]);
			lines[i] = strdup("org.freedesktop.impl.portal.FileChooser=jfm;\n");
			found_entry = 1;
		}
	}
	if (!found_entry) {
		char **nl = realloc(lines, sizeof(char *) * (nlines + (found_section ? 1 : 2)));
		lines = nl;
		if (!found_section)
			lines[nlines++] = strdup("[preferred]\n");
		lines[nlines++] = strdup("org.freedesktop.impl.portal.FileChooser=jfm;\n");
	}
	xdg_write_lines(pconf, lines, nlines);
	for (int i = 0; i < nlines; i++)
		free(lines[i]);
	free(lines);
	free(pconf);

	return 0;
}

int
xdg_uninstall_portal(void)
{
	char *dir = xdg_data_path("xdg-desktop-portal/portals");
	char *file = xdg_path_join(dir, "jfm.portal");
	unlink(file);
	free(file);
	free(dir);

	char *servdir = xdg_data_path("dbus-1/services");
	char *sfile = xdg_path_join(servdir, "org.freedesktop.impl.portal.jfm.service");
	unlink(sfile);
	free(sfile);
	free(servdir);

	char *pconf = xdg_config_path("xdg-desktop-portal/portals.conf");
	int nlines = 0;
	char **lines = xdg_read_lines(pconf, &nlines);
	int changed = 0;
	for (int i = 0; i < nlines; i++) {
		if (strncmp(lines[i], "org.freedesktop.impl.portal.FileChooser=jfm", 43) == 0) {
			free(lines[i]);
			lines[i] = strdup("");
			changed = 1;
		}
	}
	if (changed)
		xdg_write_lines(pconf, lines, nlines);
	for (int i = 0; i < nlines; i++)
		free(lines[i]);
	free(lines);
	free(pconf);

	return 0;
}
