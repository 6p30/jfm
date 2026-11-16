#include "jfm.h"

Display *dpy;
int scr;
Window win;
GC gc;
XftFont *font;

XftDraw *xftdraw;
Visual *visual;
Colormap cmap;
int depth;

Rectangle screenr;
Rectangle toolr;
Rectangle backr;
Rectangle forwardr;
Rectangle homer;
Rectangle upr;
Rectangle cdr;
Rectangle newdirr;
Rectangle newfiler;
Rectangle viewr;
Rectangle scrollr;
Rectangle scrposr;
Rectangle pathr;

XftColor c_toolbg, c_toolfg, c_viewbg, c_viewfg, c_selbg, c_selfg;
XftColor c_scrollbg, c_scrollfg, c_high, c_alertbg, c_alertfg;
XftColor c_sel2bg, c_sel2fg;

Dir *dirs;
int ndirs;
char path[4096];
char *home;
int sizew;
int lineh;
int nlines;
int offset;
int scrolling;
int oldbuttons;
int lastn;
int selected = -1;

Config *cfg;
int pickermode;
int printuri;
int showhidden;
char *wintitle;
Atom wmdelete;
Rectangle okr;
Rectangle cancelr;

static char *portaltoken;
static char *portaldest;
static char *portaltitle;
static char *portaldir;
static char *portalmode;
static char *portalname;

enum {
	ModeNormal,
	ModeOpen,
	ModeDirectory,
	ModeMultiple,
	ModeSave,
	ModeUpload,
};

static int *selsel;
static int nselsel;

static char *savename;

static int dragidx = -1;
static int droptarget = -1;
static int dragmoved;

static char **hist;
static int nhist;
static int histidx = -1;
static int histnav;

static char *accepttypes;

static int
mime_accepted(const char *path)
{
	if (!accepttypes || !*accepttypes)
		return 1;
	char *mime = xdg_mime_of(path);
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	int rc = 0;
	char *copy = strdup(accepttypes);
	char *save = nil;
	for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(nil, ",", &save)) {
		while (*tok == ' ')
			tok++;
		size_t tl = strlen(tok);
		while (tl > 0 && tok[tl - 1] == ' ')
			tok[--tl] = 0;
		if (!*tok)
			continue;
		if (strcmp(tok, "*") == 0 || strcmp(tok, "*/*") == 0 || strcasecmp(tok, "all") == 0) {
			rc = 1;
			break;
		}
		if (strcmp(tok, mime) == 0) {
			rc = 1;
			break;
		}
		size_t len = strlen(tok);
		if (len > 2 && tok[len - 2] == '/' && tok[len - 1] == '*') {
			if (strncmp(mime, tok, len - 1) == 0) {
				rc = 1;
				break;
			}
		}
		if (strchr(tok, '*') || strchr(tok, '?') || tok[0] == '.') {
			char pat[256];
			if (tok[0] == '.')
				snprintf(pat, sizeof(pat), "*%s", tok);
			else
				snprintf(pat, sizeof(pat), "%s", tok);
			if (fnmatch(pat, base, FNM_CASEFOLD) == 0) {
				rc = 1;
				break;
			}
		}
	}
	free(copy);
	free(mime);
	return rc;
}

void
selclear(void)
{
	free(selsel);
	selsel = nil;
	nselsel = 0;
	selected = -1;
}

void
seladd(int idx)
{
	if (idx < 0)
		return;
	if (selhas(idx))
		return;
	selsel = realloc(selsel, sizeof(int) * (nselsel + 1));
	selsel[nselsel++] = idx;
	selected = idx;
}

void
seltoggle(int idx)
{
	if (idx < 0)
		return;
	for (int i = 0; i < nselsel; i++) {
		if (selsel[i] != idx)
			continue;
		for (int j = i; j < nselsel - 1; j++)
			selsel[j] = selsel[j + 1];
		nselsel--;
		if (nselsel == 0)
			selected = -1;
		return;
	}
	seladd(idx);
}

int
selhas(int idx)
{
	for (int i = 0; i < nselsel; i++)
		if (selsel[i] == idx)
			return 1;
	return 0;
}

char *
expandtilde(char *s)
{
	if (s[0] != '~')
		return strdup(s);
	if (s[1] == '/' || s[1] == '\0') {
		char *expanded = smprint("%s%s", home, s + 1);
		return expanded;
	}
	return strdup(s);
}

char *
cleanname(char *s)
{
	char *p, *q, *dotdot;
	int rooted;

	rooted = s[0] == '/';
	p = q = dotdot = s + rooted;

	while (*p) {
		while (*p == '/')
			p++;

		if (p[0] == '.' && (p[1] == '/' || p[1] == 0)) {
			p += 1 + (p[1] == '/');
			continue;
		}

		if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == 0)) {
			p += 2 + (p[2] == '/');
			if (q > dotdot) {
				q--;
				while (q > dotdot && *(q - 1) != '/')
					q--;
			} else if (!rooted) {
				if (q != s)
					*q++ = '/';
				*q++ = '.';
				*q++ = '.';
				dotdot = q;
			}
			continue;
		}

		if (q != dotdot && *(q - 1) != '/')
			*q++ = '/';
		while (*p && *p != '/')
			*q++ = *p++;
	}

	if (q == s)
		*q++ = '.';
	if (q > dotdot && *(q - 1) == '/')
		q--;
	*q = 0;

	return s;
}

void
xfillrect(Rectangle r, XftColor c)
{
	XSetForeground(dpy, gc, c.pixel);
	XFillRectangle(dpy, win, gc, r.min.x, r.min.y, Dx(r), Dy(r));
}

void
xdrawrect(Rectangle r, XftColor c)
{
	XSetForeground(dpy, gc, c.pixel);
	XDrawRectangle(dpy, win, gc, r.min.x, r.min.y, Dx(r) - 1, Dy(r) - 1);
}

void
xdrawline(Point p1, Point p2, XftColor c)
{
	XSetForeground(dpy, gc, c.pixel);
	XDrawLine(dpy, win, gc, p1.x, p1.y, p2.x, p2.y);
}

void
xdrawstring(Point p, XftColor c, const char *s)
{
	XftDrawStringUtf8(xftdraw, &c, font, p.x, p.y, (FcChar8 *)s, strlen(s));
}

int
xstringwidth(const char *s)
{
	XGlyphInfo ext;
	XftTextExtentsUtf8(dpy, font, (FcChar8 *)s, strlen(s), &ext);
	return ext.width;
}

Point
drawtext(Point p, XftColor col, char *t, int maxx)
{
	char *s = t;
	if (*s && (p.x + xstringwidth(s)) > maxx) {
		int ew = xstringwidth("…");
		xdrawstring(p, col, "…");
		p.x += ew;
		while (*s && (p.x + xstringwidth(s)) > maxx) {
			int n = 1;
			unsigned char c = *s;
			if ((c & 0x80) == 0) n = 1;
			else if ((c & 0xe0) == 0xc0) n = 2;
			else if ((c & 0xf0) == 0xe0) n = 3;
			else if ((c & 0xf8) == 0xf0) n = 4;
			s += n;
		}
		xdrawstring(p, col, s);
		p.x += xstringwidth(s);
	} else {
		xdrawstring(p, col, s);
		p.x += xstringwidth(s);
	}
	return p;
}

Rectangle
drawbutton(Point *p, XftColor col, char *label)
{
	p->x += Toolpadding;
	int w = xstringwidth(label);
	int h = font->height;
	Rectangle r = Rect(p->x, p->y, p->x + w, p->y + h);
	p->y += (Dy(r) - font->height) / 2 + font->ascent;
	xdrawstring(*p, col, label);
	p->y -= (Dy(r) - font->height) / 2 + font->ascent;
	p->x += w + Toolpadding;
	return r;
}

char *
abspath(char *wd, char *p)
{
	if (p != nil && strncmp(p, "file://", 7) == 0) {
		char *real = xdg_uri_to_path(p);
		char *clean = cleanname(real);
		return clean;
	}
	if (p[0] == '/') {
		char *s = strdup(p);
		return cleanname(s);
	} else {
		int n = strlen(wd) + strlen(p) + 2;
		char *s = malloc(n);
		snprintf(s, n, "%s/%s", wd, p);
		return cleanname(s);
	}
}

int
dircmp(const void *a, const void *b)
{
	const Dir *da = a, *db = b;
	if (da->isdir == db->isdir)
		return strcmp(da->name, db->name);
	return da->isdir ? -1 : 1;
}

void
loaddirs(void)
{
	DIR *d = opendir(path);
	if (!d) {
		showerrstr("Unable to load directory");
		return;
	}
	if (dirs) {
		for (int i = 0; i < ndirs; i++)
			free(dirs[i].name);
		free(dirs);
	}
	struct dirent *de;
	int count = 0;
	while ((de = readdir(d)))
		count++;
	rewinddir(d);
	dirs = calloc(count, sizeof(Dir));
	ndirs = 0;
	while ((de = readdir(d))) {
		if (strcmp(de->d_name, ".") == 0)
			continue;
		if (!showhidden && de->d_name[0] == '.')
			continue;
		char fullpath[8192];
		snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
		struct stat st;
		if (stat(fullpath, &st) < 0)
			continue;
		if (accepttypes && !S_ISDIR(st.st_mode) && !mime_accepted(fullpath))
			continue;
		dirs[ndirs].name = strdup(de->d_name);
		dirs[ndirs].length = st.st_size;
		dirs[ndirs].mtime = st.st_mtime;
		dirs[ndirs].isdir = S_ISDIR(st.st_mode);
		ndirs++;
	}
	closedir(d);
	qsort(dirs, ndirs, sizeof(Dir), dircmp);
	offset = 0;
	selclear();
	long long m = 1;
	for (int i = 0; i < ndirs; i++)
		if (dirs[i].length > m)
			m = dirs[i].length;
	sizew = m == 0 ? 3 : 1 + 1 + (int)log10((double)m);
}

static void
histpush(const char *p)
{
	if (histidx >= 0 && histidx < nhist && strcmp(hist[histidx], p) == 0)
		return;
	for (int i = histidx + 1; i < nhist; i++)
		free(hist[i]);
	nhist = histidx + 1;
	hist = realloc(hist, sizeof(char *) * (nhist + 1));
	hist[nhist++] = strdup(p);
	histidx = nhist - 1;
}

void
up(void)
{
	cd("..");
}

int
cd(char *dir)
{
	char newpath[8192] = {0};
	if (dir == nil)
		snprintf(newpath, sizeof(newpath), "%s", home);
	else if (strncmp(dir, "file://", 7) == 0) {
		char *p = xdg_uri_to_path(dir);
		snprintf(newpath, sizeof(newpath), "%s", p);
		free(p);
	} else if (dir[0] == '~') {
		char *expanded = expandtilde(dir);
		snprintf(newpath, sizeof(newpath), "%s", expanded);
		free(expanded);
	} else if (dir[0] == '/')
		snprintf(newpath, sizeof(newpath), "%s", dir);
	else {
		int plen = strlen(path);
		if (plen > 0 && path[plen - 1] == '/')
			snprintf(newpath, sizeof(newpath), "%s%s", path, dir);
		else
			snprintf(newpath, sizeof(newpath), "%s/%s", path, dir);
	}
	struct stat st;
	if (stat(newpath, &st) < 0) {
		showerrstr("Directory does not exist");
		return 0;
	}
	if (!S_ISDIR(st.st_mode)) {
		alert("Not a directory", "The path does not point to a directory.");
		return 0;
	}
	char *tmp = abspath(path, newpath);
	strncpy(path, tmp, sizeof(path) - 1);
	path[sizeof(path) - 1] = 0;
	free(tmp);
	loaddirs();
	if (!histnav)
		histpush(path);
	return 1;
}

static void
histgoto(int idx)
{
	if (idx < 0 || idx >= nhist)
		return;
	char *target = strdup(hist[idx]);
	int old = histidx;
	histnav = 1;
	if (cd(target))
		histidx = idx;
	else
		histidx = old;
	histnav = 0;
	free(target);
	redraw();
}

static void
back(void)
{
	histgoto(histidx - 1);
}

static void
forward(void)
{
	histgoto(histidx + 1);
}

void
mk_dir(char *name)
{
	char *p = smprint("%s/%s", path, name);
	if (access(p, F_OK) >= 0) {
		showerrstr("Directory already exists");
		free(p);
		return;
	}
	if (mkdir(p, 0755) < 0)
		showerrstr("Unable to create directory");
	else
		loaddirs();
	free(p);
}

void
touch(char *name)
{
	char *p = smprint("%s/%s", path, name);
	if (access(p, F_OK) >= 0) {
		showerrstr("File already exists");
		free(p);
		return;
	}
	int fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0)
		showerrstr("Unable to create file");
	else {
		close(fd);
		loaddirs();
	}
	free(p);
}

int
doexec(const char *cmd)
{
	int rc = system(cmd);
	if (rc < 0)
		return -1;
	if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0)
		return 0;
	return -1;
}

char *
quotestrdup(const char *s)
{
	int n = 2;
	for (const char *p = s; *p; p++) {
		if (*p == '\'')
			n += 4;
		else
			n++;
	}
	char *q = malloc(n + 1);
	char *d = q;
	*d++ = '\'';
	for (const char *p = s; *p; p++) {
		if (*p == '\'') {
			*d++ = '\'';
			*d++ = '\\';
			*d++ = '\'';
			*d++ = '\'';
		} else {
			*d++ = *p;
		}
	}
	*d++ = '\'';
	*d = 0;
	return q;
}

void
rm(Dir d)
{
	char buf[1024] = {0};
	if (d.isdir)
		snprintf(buf, sizeof(buf), "Delete directory '%s' and its subdirectories ?", d.name);
	else
		snprintf(buf, sizeof(buf), "Delete file '%s' ?", d.name);
	if (!confirm(buf))
		return;
	char *p = smprint("%s/%s", path, d.name);
	char *qp = quotestrdup(p);
	char *cmd = smprint("rm -r %s >/dev/null 2>&1", qp);
	if (doexec(cmd) < 0)
		alert("Cannot remove file/directory", "Check permissions or try again.");
	else
		loaddirs();
	free(cmd);
	free(qp);
	free(p);
}

void
mv(char *from, char *to)
{
	char *fp = smprint("%s/%s", path, from);
	char *tp = smprint("%s/%s", path, to);
	char *qfp = quotestrdup(fp);
	char *qtp = quotestrdup(tp);
	char *cmd = smprint("mv %s %s >/dev/null 2>&1", qfp, qtp);
	if (doexec(cmd) < 0)
		alert("Cannot rename", "Check that the destination is valid and you have permission.");
	else
		loaddirs();
	free(cmd);
	free(qtp);
	free(qfp);
	free(tp);
	free(fp);
}

void
movefile(char *from, char *dest)
{
	char *qfrom = quotestrdup(from);
	char *qdest = quotestrdup(dest);
	char *cmd = smprint("mv %s %s >/dev/null 2>&1", qfrom, qdest);
	if (doexec(cmd) < 0)
		alert("Move failed", "Check that the destination directory exists and is writable.");
	else
		loaddirs();
	free(cmd);
	free(qdest);
	free(qfrom);
}

int
plumbfile(char *dir, char *name)
{
	char *f = smprint("%s/%s", dir, name);
	if (access(f, F_OK) != 0) {
		alert("File does not exist anymore", nil);
		free(f);
		loaddirs();
		redraw();
		return 0;
	}
	xdg_open_path(f);
	free(f);
	return 1;
}

void
config_save_state(void)
{
	if (!cfg)
		return;
	char buf[64];
	if (fontname)
		config_set(cfg, "font", fontname);
	snprintf(buf, sizeof(buf), "%d", Dx(screenr));
	config_set(cfg, "width", buf);
	snprintf(buf, sizeof(buf), "%d", Dy(screenr));
	config_set(cfg, "height", buf);
	snprintf(buf, sizeof(buf), "%d", showhidden);
	config_set(cfg, "showhidden", buf);
	config_save(cfg);
}

static void
picker_emit(char **paths, int n)
{
	for (int i = 0; i < n; i++) {
		if (printuri) {
			char *uri = xdg_path_to_uri(paths[i]);
			printf("%s\n", uri);
			free(uri);
		} else {
			printf("%s\n", paths[i]);
		}
	}
	fflush(stdout);
}

void
picker_cancel(void)
{
	if (portaltoken && portaldest)
		dbus_portal_deliver(portaldest, portaltoken, nil, 0);
	config_save_state();
	exit(1);
}

void
picker_accept(void)
{
	char **paths = nil;
	int n = 0;

	if (pickermode == ModeSave) {
		char buf[4096] = {0};
		if (selected >= 0 && selected < ndirs && dirs[selected].isdir) {
			cd(dirs[selected].name);
			redraw();
			return;
		}
		if (savename && *savename)
			snprintf(buf, sizeof(buf), "%s", savename);
		else if (selected >= 0 && selected < ndirs)
			snprintf(buf, sizeof(buf), "%s", dirs[selected].name);
		for (;;) {
			if (enter("Save as", buf, sizeof(buf)) <= 0) {
				picker_cancel();
				return;
			}
			if (!buf[0])
				continue;
			char *expanded = nil;
			if (strncmp(buf, "file://", 7) == 0)
				expanded = xdg_uri_to_path(buf);
			else if (buf[0] == '~')
				expanded = expandtilde(buf);
			else
				expanded = strdup(buf);
			char *dest = abspath(path, expanded);
			free(expanded);
			struct stat st;
			if (stat(dest, &st) == 0 && S_ISDIR(st.st_mode)) {
				cd(buf);
				free(dest);
				redraw();
				return;
			}
			if (access(dest, F_OK) == 0) {
				char *msg = smprint("Replace %s?", buf);
				int yes = confirm(msg);
				free(msg);
				if (!yes) {
					free(dest);
					continue;
				}
			}
			paths = malloc(sizeof(char *));
			paths[0] = dest;
			n = 1;
			break;
		}
	} else if (pickermode == ModeOpen) {
		if (selected >= 0 && selected < ndirs && dirs[selected].isdir) {
			cd(dirs[selected].name);
			redraw();
			return;
		}
		for (int i = 0; i < nselsel; i++) {
			if (selsel[i] < 0 || selsel[i] >= ndirs)
				continue;
			if (dirs[selsel[i]].isdir)
				continue;
			paths = realloc(paths, sizeof(char *) * (n + 1));
			paths[n++] = smprint("%s/%s", path, dirs[selsel[i]].name);
		}
	} else {
		for (int i = 0; i < nselsel; i++) {
			if (selsel[i] < 0 || selsel[i] >= ndirs)
				continue;
			if (pickermode == ModeUpload && dirs[selsel[i]].isdir)
				continue;
			paths = realloc(paths, sizeof(char *) * (n + 1));
			paths[n++] = smprint("%s/%s", path, dirs[selsel[i]].name);
		}
		if (n == 0 && pickermode == ModeDirectory) {
			paths = malloc(sizeof(char *));
			paths[0] = strdup(path);
			n = 1;
		}
	}
	if (n == 0) {
		picker_cancel();
		return;
	}
	if (portaltoken && portaldest) {
		char **uris = calloc(n + 1, sizeof(char *));
		for (int i = 0; i < n; i++)
			uris[i] = xdg_path_to_uri(paths[i]);
		dbus_portal_deliver(portaldest, portaltoken, uris, n);
		for (int i = 0; i < n; i++)
			free(uris[i]);
		free(uris);
	} else {
		picker_emit(paths, n);
	}
	for (int i = 0; i < n; i++)
		free(paths[i]);
	free(paths);
	config_save_state();
	exit(0);
}

void
alert(const char *msg, const char *err)
{
	int h = Padding + font->height + Padding;
	if (err)
		h += font->height;
	int mw = xstringwidth(msg);
	int ew = err ? xstringwidth(err) : 0;
	int w = Padding + max(mw, ew) + Padding;
	Point o = addpt(screenr.min, Pt((Dx(screenr) - w) / 2, (Dy(screenr) - h) / 2));
	Rectangle r = Rect(o.x, o.y, o.x + w, o.y + h);

	int done = 0;
	while (!done) {
		xfillrect(r, c_alertbg);
		XSetForeground(dpy, gc, c_alertfg.pixel);
		XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
		XDrawRectangle(dpy, win, gc, r.min.x, r.min.y, Dx(r) - 1, Dy(r) - 1);
		XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);

		Point p = addpt(o, Pt(Padding, Padding + font->ascent));
		xdrawstring(p, c_alertfg, msg);
		if (err) {
			p.y += font->height;
			xdrawstring(p, c_alertfg, err);
		}
		XFlush(dpy);

		XEvent e;
		XNextEvent(dpy, &e);
		if (e.type == KeyPress) {
			KeySym k;
			XLookupString(&e.xkey, nil, 0, &k, nil);
			if (k == XK_Return || k == XK_Escape)
				done = 1;
		} else if (e.type == ButtonPress) {
			if (ptinrect(Pt(e.xbutton.x, e.xbutton.y), r))
				done = 1;
		} else if (e.type == Expose) {
		}
	}
	redraw();
}

int
confirm(const char *msg)
{
	int h = Padding + font->height + Padding;
	int w = Padding + xstringwidth(msg) + xstringwidth(" Yes / No") + Padding;
	Point o = addpt(screenr.min, Pt((Dx(screenr) - w) / 2, (Dy(screenr) - h) / 2));
	Rectangle r = Rect(o.x, o.y, o.x + w, o.y + h);

	int done = 0, rc = 0;
	while (!done) {
		xfillrect(r, c_viewbg);
		xdrawrect(r, c_high);

		Point p = addpt(o, Pt(Padding, Padding + font->ascent));
		p = drawtext(p, c_viewfg, (char *)msg, r.max.x - Padding);
		p = drawtext(p, c_high, " Y", r.max.x - Padding);
		p = drawtext(p, c_viewfg, "es /", r.max.x - Padding);
		p = drawtext(p, c_high, " N", r.max.x - Padding);
		drawtext(p, c_viewfg, "o", r.max.x - Padding);
		XFlush(dpy);

		XEvent e;
		XNextEvent(dpy, &e);
		if (e.type == KeyPress) {
			KeySym k;
			XLookupString(&e.xkey, nil, 0, &k, nil);
			if (k == XK_Escape || k == XK_n || k == XK_N) {
				done = 1;
				rc = 0;
			} else if (k == XK_y || k == XK_Y || k == XK_Return) {
				done = 1;
				rc = 1;
			}
		} else if (e.type == ButtonPress) {
			if (ptinrect(Pt(e.xbutton.x, e.xbutton.y), r)) {
				done = 1;
				rc = 0;
			}
		}
	}
	redraw();
	return rc;
}

int
enter(const char *prompt, char *buf, int nbuf)
{
	int pw = xstringwidth(prompt);
	int w = Padding + pw + 300 + Padding;
	int h = Padding + font->height + Padding + font->height + Padding + 4;
	Point o = addpt(screenr.min, Pt((Dx(screenr) - w) / 2, (Dy(screenr) - h) / 2));
	Rectangle r = Rect(o.x, o.y, o.x + w, o.y + h);

	int len = strlen(buf);
	int done = 0, rc = 0;
	while (!done) {
		xfillrect(r, c_viewbg);
		xdrawrect(r, c_high);

		Point p = addpt(o, Pt(Padding, Padding + font->ascent));
		xdrawstring(p, c_viewfg, prompt);
		p.y += font->height + Padding;
		xdrawstring(p, c_viewfg, buf);
		int cw = xstringwidth(buf);
		XSetForeground(dpy, gc, c_viewfg.pixel);
		XDrawLine(dpy, win, gc, p.x + cw, p.y - font->ascent, p.x + cw, p.y + font->descent);
		XFlush(dpy);

		XEvent e;
		XNextEvent(dpy, &e);
		if (e.type == KeyPress) {
			KeySym k;
			char str[32];
			int n = XLookupString(&e.xkey, str, sizeof(str), &k, nil);
			if (k == XK_Escape) {
				done = 1;
				rc = 0;
			} else if (k == XK_Return) {
				done = 1;
				rc = len;
			} else if (k == XK_BackSpace && len > 0) {
				buf[--len] = 0;
			} else if (n == 1 && isprint((unsigned char)str[0]) && len < nbuf - 1) {
				buf[len++] = str[0];
				buf[len] = 0;
			}
		}
	}
	redraw();
	return rc;
}

int
menuhit(int x, int y, char **items, int nitems)
{
	int itemh = font->height + Padding;
	int w = 0;
	for (int i = 0; i < nitems; i++) {
		int tw = xstringwidth(items[i]);
		if (tw > w)
			w = tw;
	}
	w += 2 * Padding;
	int h = nitems * itemh + Padding;
	Rectangle r = Rect(x, y, x + w, y + h);
	if (r.max.x > screenr.max.x) {
		int dx = Dx(r);
		r.min.x = screenr.max.x - dx;
		r.max.x = screenr.max.x;
	}
	if (r.max.y > screenr.max.y) {
		int dy = Dy(r);
		r.min.y = screenr.max.y - dy;
		r.max.y = screenr.max.y;
	}

	int sel = -1;
	int done = 0;
	while (!done) {
		xfillrect(r, c_viewbg);
		xdrawrect(r, c_high);
		for (int i = 0; i < nitems; i++) {
			Point p = addpt(r.min, Pt(Padding, Padding + font->ascent + i * itemh));
			xdrawstring(p, c_viewfg, items[i]);
		}
		XFlush(dpy);

		XEvent e;
		XNextEvent(dpy, &e);
		if (e.type == ButtonRelease) {
			Point p = Pt(e.xbutton.x, e.xbutton.y);
			if (ptinrect(p, r)) {
				sel = (p.y - r.min.y) / itemh;
				if (sel < 0 || sel >= nitems)
					sel = -1;
			}
			done = 1;
		}
	}
	redraw();
	return sel;
}

char *
mdate(Dir d)
{
	char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm *tm = localtime(&d.mtime);
	char *s = malloc(32);
	snprintf(s, 32, "%s %02d %02d:%02d", months[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min);
	return s;
}

static void
drawdircol(int n, XftColor *bg, XftColor *fg)
{
	if (n < 0 || offset + n < 0 || offset + n >= ndirs)
		return;
	Dir *d = &dirs[offset + n];

	Point p = addpt(viewr.min, Pt(Toolpadding, Toolpadding));
	p.y += n * lineh;
	Rectangle r = Rect(p.x, p.y, viewr.max.x - Toolpadding, p.y + lineh);
	xfillrect(r, *bg);

	char *t = mdate(*d);
	char buf[256];
	snprintf(buf, sizeof(buf), "%*lld  %s", sizew, d->length, t);
	free(t);

	char *prefix = d->isdir ? "d" : "-";
	int pw = xstringwidth(prefix);
	Point pp = addpt(p, Pt(0, (lineh - font->height) / 2 + font->ascent));
	xdrawstring(pp, *fg, prefix);
	p.x += pw + 4 + Padding;
	p.y += (lineh - font->height) / 2 + font->ascent;
	p = drawtext(p, *fg, d->name, viewr.max.x - xstringwidth(buf) - 2 * Padding - Toolpadding);
	p.x = viewr.max.x - xstringwidth(buf) - 2 * Padding - Toolpadding;
	xdrawstring(p, *fg, buf);
}

void
drawdir(int n, int isselected)
{
	if (isselected)
		drawdircol(n, &c_sel2bg, &c_sel2fg);
	else
		drawdircol(n, &c_viewbg, &c_viewfg);
}

static void
drawdrop(int n)
{
	if (n < 0 || offset + n >= ndirs)
		return;
	drawdircol(n, &c_selbg, &c_selfg);
	Rectangle r = Rect(viewr.min.x + Toolpadding,
			   viewr.min.y + Toolpadding + n * lineh,
			   viewr.max.x - Toolpadding,
			   viewr.min.y + Toolpadding + (n + 1) * lineh);
	xdrawrect(r, c_high);
}

static void
setdroptarget(int idx)
{
	if (idx == droptarget)
		return;
	if (droptarget >= 0) {
		drawdir(droptarget - offset, selhas(droptarget));
		droptarget = -1;
	}
	if (idx >= 0 && idx < ndirs && dirs[idx].isdir && idx != dragidx) {
		droptarget = idx;
		drawdrop(idx - offset);
	}
	XFlush(dpy);
}

static void
cleardroptarget(void)
{
	if (droptarget >= 0) {
		drawdir(droptarget - offset, selhas(droptarget));
		droptarget = -1;
		XFlush(dpy);
	}
}

void
flash(int n)
{
	for (int i = 0; i < 5; i++) {
		drawdir(n, i & 1);
		XFlush(dpy);
		usleep(50000);
	}
}

void
redraw(void)
{
	xfillrect(screenr, c_viewbg);

	xfillrect(toolr, c_toolbg);
	xdrawline(Pt(toolr.min.x, toolr.max.y), Pt(toolr.max.x, toolr.max.y), c_toolfg);

	Point p = addpt(toolr.min, Pt(0, Toolpadding));
	backr = drawbutton(&p, c_toolfg, "<");
	forwardr = drawbutton(&p, c_toolfg, ">");
	homer = drawbutton(&p, c_toolfg, "~");
	cdr = drawbutton(&p, c_toolfg, "@");
	upr = drawbutton(&p, c_toolfg, "^");
	p.x += Toolpadding;
	int rightw = 2 * Toolpadding + xstringwidth("+") + 2 * Toolpadding + xstringwidth("f");
	if (pickermode)
		rightw += 2 * Toolpadding + xstringwidth("OK") + 2 * Toolpadding + xstringwidth("Cancel");
	int pathmax = screenr.max.x - rightw - Padding;
	p.y = toolr.min.y + (Dy(toolr) - font->height) / 2 + font->ascent;
	int pw = xstringwidth(path);
	int px2 = p.x + pw;
	if (px2 > pathmax)
		px2 = pathmax;
	if (px2 < p.x)
		px2 = p.x;
	pathr = Rect(p.x, p.y - font->ascent, px2, p.y + font->descent);
	p = drawtext(p, c_toolfg, path, pathmax);
	p.x = screenr.max.x - rightw;
	p.y = toolr.min.y + Toolpadding;
	newdirr = drawbutton(&p, c_toolfg, "+");
	newfiler = drawbutton(&p, c_toolfg, "f");
	if (pickermode) {
		okr = drawbutton(&p, c_toolfg, "OK");
		cancelr = drawbutton(&p, c_toolfg, "Cancel");
	} else {
		okr = Rect(0, 0, 0, 0);
		cancelr = Rect(0, 0, 0, 0);
	}

	xfillrect(scrollr, c_scrollbg);
	if (ndirs > 0) {
		int h = ((double)nlines / ndirs) * Dy(scrollr);
		int y = ((double)offset / ndirs) * Dy(scrollr);
		scrposr = Rect(scrollr.min.x, scrollr.min.y + y, scrollr.max.x - 1, scrollr.min.y + y + h);
	} else {
		scrposr = scrollr;
	}
	xfillrect(scrposr, c_scrollfg);

	for (int i = 0; i < nlines && offset + i < ndirs; i++)
		drawdir(i, selhas(offset + i) ? 1 : 0);

	XFlush(dpy);
}

int
scrollclamp(int off)
{
	if (nlines >= ndirs)
		return 0;
	if (off < 0)
		return 0;
	if (off + nlines > ndirs)
		return ndirs - nlines;
	return off;
}

void
scrollup(int off)
{
	int newoff = scrollclamp(offset - off);
	if (newoff != offset) {
		offset = newoff;
		redraw();
	}
}

void
scrolldown(int off)
{
	int newoff = scrollclamp(offset + off);
	if (newoff != offset) {
		offset = newoff;
		redraw();
	}
}

void
evtresize(int w, int h)
{
	screenr = Rect(0, 0, w, h);
	lineh = Padding + font->height + Padding;
	toolr = Rect(0, 0, w, 16 + 2 * Toolpadding);
	scrollr = Rect(0, toolr.max.y, Scrollwidth, h);
	viewr = Rect(Scrollwidth, toolr.max.y, w, h);
	nlines = Dy(viewr) / lineh;
	if (xftdraw)
		XftDrawDestroy(xftdraw);
	xftdraw = XftDrawCreate(dpy, win, visual, cmap);
	redraw();
}

static void
selmove(int delta)
{
	if (ndirs <= 0)
		return;
	int idx = selected;
	if (idx < 0)
		idx = delta > 0 ? -1 : ndirs;
	idx += delta;
	if (idx < 0)
		idx = 0;
	if (idx >= ndirs)
		idx = ndirs - 1;
	selclear();
	seladd(idx);
	if (idx < offset)
		offset = idx;
	if (idx >= offset + nlines)
		offset = idx - nlines + 1;
	offset = scrollclamp(offset);
	redraw();
}

void
evtkey(XEvent *e)
{
	KeySym k;
	XLookupString(&e->xkey, nil, 0, &k, nil);
	switch (k) {
	case XK_Escape:
		if (pickermode)
			picker_cancel();
		break;
	case XK_q:
	case XK_Delete:
		if (pickermode)
			picker_cancel();
		config_save_state();
		exit(0);
	case XK_Page_Up:
		scrollup(nlines);
		break;
	case XK_Page_Down:
		scrolldown(nlines);
		break;
	case XK_Home:
		cd(home);
		redraw();
		break;
	case XK_Left:
		if (e->xkey.state & Mod1Mask)
			back();
		break;
	case XK_Right:
		if (e->xkey.state & Mod1Mask)
			forward();
		break;
	case XK_BackSpace:
		if (e->xkey.state & Mod1Mask) {
			up();
			redraw();
		}
		break;
	case XK_Up:
		if (pickermode)
			selmove(-1);
		else {
			up();
			redraw();
		}
		break;
	case XK_Down:
		if (pickermode)
			selmove(1);
		break;
	case XK_Return:
		if (selected >= 0 && selected < ndirs) {
			Dir *d = &dirs[selected];
			if (d->isdir) {
				if (pickermode == ModeDirectory)
					picker_accept();
				else {
					cd(d->name);
					redraw();
				}
			} else if (pickermode) {
				picker_accept();
			} else {
				plumbfile(path, d->name);
				flash(selected - offset);
			}
		} else if (pickermode) {
			picker_accept();
		}
		break;
	case XK_space:
		break;
	case XK_f:
	case XK_F:
		cyclefont();
		break;
	}
}

Point
cept(const char *text)
{
	Point p;
	p.x = screenr.min.x + (Dx(screenr) - xstringwidth(text) - 4) / 2;
	p.y = screenr.min.y + (Dy(screenr) - font->height - 4) / 2;
	return p;
}

int
indexat(Point p)
{
	if (!ptinrect(p, viewr))
		return -1;
	int n = (p.y - viewr.min.y) / lineh;
	if (offset + n >= ndirs)
		return -1;
	return offset + n;
}

static void
openwith(Point mxy, int idx)
{
	char *full = smprint("%s/%s", path, dirs[idx].name);
	char *mime = xdg_mime_of(full);
	int n = 0;
	char **apps = xdg_mime_apps(mime, &n);
	if (n == 0) {
		alert("No application found", "No handler is registered for this file type.");
	} else {
		char **items = calloc(n, sizeof(char *));
		for (int i = 0; i < n; i++) {
			char *nm = xdg_desktop_field(apps[i], "Name");
			items[i] = nm ? nm : strdup(apps[i]);
		}
		int s = menuhit(mxy.x, mxy.y, items, n);
		if (s >= 0 && s < n)
			xdg_run_desktop(apps[s], full);
		for (int i = 0; i < n; i++)
			free(items[i]);
		free(items);
	}
	for (int i = 0; i < n; i++)
		free(apps[i]);
	free(apps);
	free(mime);
	free(full);
}

static void
scrolltrack(Point mxy)
{
	if (nlines >= ndirs) {
		if (offset != 0) {
			offset = 0;
			redraw();
		}
		return;
	}
	int y = mxy.y - scrollr.min.y;
	int range = ndirs - nlines;
	int newoff = scrollclamp(y * range / Dy(scrollr));
	if (newoff != offset) {
		offset = newoff;
		redraw();
	}
}

static void
move_selected_to(const char *destdir, int dragged)
{
	char *destabs = abspath(path, (char *)destdir);
	struct stat st;
	if (stat(destabs, &st) < 0 || !S_ISDIR(st.st_mode)) {
		alert("Invalid destination", "The path is not a directory or does not exist.");
		free(destabs);
		return;
	}
	if (nselsel == 0 && dragged >= 0 && dragged < ndirs)
		seladd(dragged);
	int nex = 0, nfail = 0;
	for (int i = 0; i < nselsel; i++) {
		int idx = selsel[i];
		if (idx < 0 || idx >= ndirs)
			continue;
		char *src = smprint("%s/%s", path, dirs[idx].name);
		char *dest = smprint("%s/%s", destabs, dirs[idx].name);
		if (access(dest, F_OK) == 0) {
			nex++;
		} else {
			char *qfrom = quotestrdup(src);
			char *qdest = quotestrdup(dest);
			char *cmd = smprint("mv %s %s >/dev/null 2>&1", qfrom, qdest);
			if (doexec(cmd) < 0)
				nfail++;
			free(cmd);
			free(qdest);
			free(qfrom);
		}
		free(src);
		free(dest);
	}
	free(destabs);
	if (nfail > 0)
		alert("Move failed", "Check that the destination is valid and you have permission.");
	else if (nex > 0)
		alert("Move skipped", "Some files were not moved because a file with the same name exists.");
	loaddirs();
	redraw();
}

static void
pathprompt(void)
{
	char buf[4096];
	snprintf(buf, sizeof(buf), "%s", path);
	if (enter("Go to directory", buf, sizeof(buf)) > 0)
		cd(buf);
	redraw();
}

static void
gotoprompt(void)
{
	char buf[4096] = {0};
	if (enter("Go to directory", buf, sizeof(buf)) > 0)
		cd(buf);
	redraw();
}

static int
toolbarclick(Point mxy)
{
	if (ptinrect(mxy, backr)) {
		back();
		return 1;
	}
	if (ptinrect(mxy, forwardr)) {
		forward();
		return 1;
	}
	if (ptinrect(mxy, homer)) {
		cd(home);
		redraw();
		return 1;
	}
	if (ptinrect(mxy, upr)) {
		up();
		redraw();
		return 1;
	}
	if (ptinrect(mxy, cdr)) {
		gotoprompt();
		return 1;
	}
	if (ptinrect(mxy, pathr)) {
		pathprompt();
		return 1;
	}
	if (ptinrect(mxy, newdirr)) {
		char buf[4096] = {0};
		if (enter("Create directory", buf, sizeof(buf)) > 0) {
			mk_dir(buf);
			redraw();
		}
		return 1;
	}
	if (ptinrect(mxy, newfiler)) {
		char buf[4096] = {0};
		if (enter("Create file", buf, sizeof(buf)) > 0) {
			touch(buf);
			redraw();
		}
		return 1;
	}
	if (ptinrect(mxy, okr)) {
		picker_accept();
		return 1;
	}
	if (ptinrect(mxy, cancelr)) {
		picker_cancel();
		return 1;
	}
	return 0;
}

void
evtmouse(XEvent *e)
{
	static int buttons = 0;
	static Time lastclick = 0;
	static int lastclickidx = -1;
	static Point pressxy;
	Point mxy;
	int b = 0;

	if (e->type == ButtonPress) {
		b = e->xbutton.button;
		mxy = Pt(e->xbutton.x, e->xbutton.y);
		if (b == 4) {
			scrollup(Slowscroll);
			return;
		}
		if (b == 5) {
			scrolldown(Slowscroll);
			return;
		}
		if (b == 1)
			buttons |= 1;
		else if (b == 2)
			buttons |= 2;
		else if (b == 3)
			buttons |= 4;
	} else if (e->type == ButtonRelease) {
		b = e->xbutton.button;
		mxy = Pt(e->xbutton.x, e->xbutton.y);
		if (b == 1) {
			buttons &= ~1;
			if (dragidx >= 0) {
				if (dragmoved && !pickermode) {
					int idx = indexat(mxy);
					if (idx >= 0 && idx != dragidx && dirs[idx].isdir)
						move_selected_to(dirs[idx].name, dragidx);
					else if (ptinrect(mxy, upr))
						move_selected_to("..", dragidx);
					else if (ptinrect(mxy, homer))
						move_selected_to(home, dragidx);
				}
				cleardroptarget();
				dragidx = -1;
				dragmoved = 0;
			}
			scrolling = 0;
		} else if (b == 2) {
			buttons &= ~2;
			scrolling = 0;
		} else if (b == 3) {
			buttons &= ~4;
			scrolling = 0;
		}
		oldbuttons = buttons;
		return;
	} else if (e->type == MotionNotify) {
		mxy = Pt(e->xmotion.x, e->xmotion.y);
	}

	if (oldbuttons == 0 && buttons != 0 && ptinrect(mxy, scrollr))
		scrolling = 1;
	else if (buttons == 0)
		scrolling = 0;

	if (buttons & 1) {
		if (scrolling) {
			if (ptinrect(mxy, scrollr) &&
			    (e->type == MotionNotify || e->type == ButtonPress))
				scrolltrack(mxy);
		} else if (e->type == ButtonPress && toolbarclick(mxy)) {
		} else if (ptinrect(mxy, viewr) || dragidx >= 0) {
			if (e->type == ButtonPress) {
				int idx = indexat(mxy);
				pressxy = mxy;
				if (idx >= 0) {
					Time now = e->xbutton.time;
					if (idx == lastclickidx && (now - lastclick < 400)) {
						Dir *d = &dirs[idx];
						if (d->isdir) {
							if (pickermode == ModeDirectory)
								picker_accept();
							else {
								cd(d->name);
								redraw();
							}
						} else if (pickermode) {
							picker_accept();
						} else {
							if (plumbfile(path, d->name))
								flash(idx - offset);
						}
						lastclickidx = -1;
						dragidx = -1;
					} else if (e->xbutton.state & ControlMask) {
						seltoggle(idx);
						lastclickidx = idx;
						lastclick = now;
						redraw();
						dragidx = idx;
						dragmoved = 0;
					} else {
						if (!selhas(idx)) {
							selclear();
							seladd(idx);
						}
						lastclickidx = idx;
						lastclick = now;
						redraw();
						dragidx = idx;
						dragmoved = 0;
					}
				} else {
					selclear();
					lastclickidx = -1;
					dragidx = -1;
					redraw();
				}
			} else if (e->type == MotionNotify && dragidx >= 0 && !pickermode) {
				if (!dragmoved &&
				    (abs(mxy.x - pressxy.x) + abs(mxy.y - pressxy.y) > 5))
					dragmoved = 1;
				if (ptinrect(mxy, viewr))
					setdroptarget(dragmoved ? indexat(mxy) : -1);
				else
					cleardroptarget();
			}
		}
	} else if (buttons & 2) {
		if (scrolling) {
			if (ptinrect(mxy, scrollr) &&
			    (e->type == MotionNotify || e->type == ButtonPress))
				scrolltrack(mxy);
		} else if (e->type == ButtonPress && ptinrect(mxy, viewr)) {
			int idx = indexat(mxy);
			if (idx >= 0) {
				if (pickermode) {
					seltoggle(idx);
					redraw();
				} else {
					if (!selhas(idx)) {
						selclear();
						seladd(idx);
						redraw();
					}
					char *items[] = { "open", "open with", "rename", "delete", "move" };
					int sel = menuhit(mxy.x, mxy.y, items, 5);
					if (sel == 0) {
						if (dirs[idx].isdir) {
							cd(dirs[idx].name);
							redraw();
						} else {
							if (plumbfile(path, dirs[idx].name))
								flash(idx - offset);
						}
					} else if (sel == 1) {
						openwith(mxy, idx);
					} else if (sel == 2) {
						char buf[4096] = {0};
						snprintf(buf, sizeof(buf), "%s", dirs[idx].name);
						if (enter("Rename to", buf, sizeof(buf)) > 0) {
							mv(dirs[idx].name, buf);
							if (selhas(idx)) selected = -1;
							redraw();
						}
					} else if (sel == 3) {
						rm(dirs[idx]);
						if (selhas(idx)) selected = -1;
						redraw();
					} else if (sel == 4) {
						char destdir[4096] = {0};
						if (enter("Move to directory", destdir, sizeof(destdir)) > 0) {
							char *expanded_dest = expandtilde(destdir);
							char *destdir_abs = abspath(path, expanded_dest);
							struct stat st;
							if (stat(destdir_abs, &st) < 0 || !S_ISDIR(st.st_mode)) {
								alert("Invalid destination", "The path is not a directory or does not exist.");
							} else {
								char *src = smprint("%s/%s", path, dirs[idx].name);
								char *dest = smprint("%s/%s", destdir_abs, dirs[idx].name);
								movefile(src, dest);
								free(src);
								free(dest);
								if (selhas(idx)) selected = -1;
							}
							free(destdir_abs);
							free(expanded_dest);
							redraw();
						}
					}
				}
			}
		} else if (e->type == ButtonPress && ptinrect(mxy, pathr)) {
			loaddirs();
			redraw();
		}
	} else if ((buttons & 4) && oldbuttons == 0 && e->type == ButtonPress) {
		if (ptinrect(mxy, scrollr)) {
			scrolltrack(mxy);
		} else if (!toolbarclick(mxy) && ptinrect(mxy, viewr)) {
			int idx = indexat(mxy);
			if (idx >= 0) {
				Dir *d = &dirs[idx];
				if (d->isdir) {
					cd(d->name);
					redraw();
				} else if (pickermode) {
					selclear();
					seladd(idx);
					picker_accept();
				} else {
					if (plumbfile(path, d->name))
						flash(idx - offset);
				}
			} else {
				up();
				redraw();
			}
		}
	}

	if (e->type == MotionNotify && buttons == 0) {
		int idx = indexat(mxy);
		if (idx != lastn) {
			if (lastn != -1)
				drawdir(lastn - offset, 0);
			if (idx != -1)
				drawdir(idx - offset, 1);
			lastn = idx;
			XFlush(dpy);
		}
	}

	oldbuttons = buttons;
}

void
showerrstr(const char *msg)
{
	alert(msg, strerror(errno));
}

char *
smprint(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *s;
	vasprintf(&s, fmt, ap);
	va_end(ap);
	return s;
}

void
readhome(void)
{
	home = xdg_home();
	if (!home)
		home = "/";
}

void
usage(void)
{
	fprintf(stderr,
		"usage: jfm [options] [path]\n"
		"\n"
		"  -f, --select-file       act as a file chooser and print the selected path\n"
		"  -m, --select-multiple   act as a file chooser and print every selected path\n"
		"  -d, --select-directory  act as a directory chooser\n"
		"  -s, --save-file         act as a save dialog\n"
		"      --save-as[=NAME]    act as a save dialog with an optional suggested name\n"
		"      --upload            choose one or more files (for browser uploads)\n"
		"  -S, --select            reveal/select specified item in its parent folder\n"
		"  -u, --print-uri         print file:// uris instead of plain paths\n"
		"  -a, --accept TYPES      only show files matching TYPES (mime types or globs)\n"
		"  -t, --title TITLE       set the window title\n"
		"  -D, --directory DIR     start inside DIR\n"
		"  -H, --show-hidden       show hidden files\n"
		"  -i, --install-desktop   install the desktop entry and register as default\n"
		"  -I, --uninstall-desktop remove the desktop entry\n"
		"  -b, --set-default       register jfm as the default application for folders\n"
		"  -B, --unset-default     remove jfm as the default application for folders\n"
		"  -P, --install-portal    install the desktop portal file chooser backend\n"
		"  -p, --uninstall-portal  remove the desktop portal file chooser backend\n"
		"  -M, --print-mime FILE   print the mime type of FILE\n"
		"  -O, --open-uri URI      open URI with the default application\n"
		"  -v, --version           print the version\n"
		"  -h, --help              print this help\n");
}

static void
handle_event(XEvent *e)
{
	switch (e->type) {
	case Expose:
		if (e->xexpose.count == 0)
			redraw();
		break;
	case ConfigureNotify:
		if (e->xconfigure.width != Dx(screenr) || e->xconfigure.height != Dy(screenr))
			evtresize(e->xconfigure.width, e->xconfigure.height);
		break;
	case ButtonPress:
	case ButtonRelease:
	case MotionNotify:
		evtmouse(e);
		break;
	case KeyPress:
		evtkey(e);
		break;
	case ClientMessage:
		if ((Atom)e->xclient.data.l[0] == wmdelete) {
			config_save_state();
			exit(0);
		}
		break;
	}
}

static gboolean
x_fd_cb(gint fd, GIOCondition cond, gpointer data)
{
	while (XPending(dpy)) {
		XEvent e;
		XNextEvent(dpy, &e);
		handle_event(&e);
	}
	return G_SOURCE_CONTINUE;
}

void
jfm_raise_window(void)
{
	if (!dpy || !win)
		return;
	XMapRaised(dpy, win);
	XRaiseWindow(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	XFlush(dpy);
}

void
jfm_open_folder(const char *folder)
{
	if (!folder || !*folder)
		return;
	char *path_copy = xdg_uri_to_path(folder);
	size_t len = strlen(path_copy);
	while (len > 1 && path_copy[len - 1] == '/')
		path_copy[--len] = 0;
	cd(path_copy);
	free(path_copy);
	selclear();
	redraw();
	jfm_raise_window();
}

void
jfm_reveal(char **paths, int n)
{
	if (n <= 0)
		return;
	char *first = xdg_uri_to_path(paths[0]);
	size_t flen = strlen(first);
	while (flen > 1 && first[flen - 1] == '/')
		first[--flen] = 0;

	struct stat st;
	if (stat(first, &st) == 0 && S_ISDIR(st.st_mode)) {
		char *slash = strrchr(first, '/');
		if (slash && slash != first) {
			*slash = 0;
			cd(first);
		} else if (slash == first) {
			cd("/");
		} else {
			cd(first);
		}
	} else {
		char *slash = strrchr(first, '/');
		if (slash) {
			if (slash == first) {
				cd("/");
			} else {
				*slash = 0;
				cd(first);
			}
		}
	}
	free(first);

	selclear();
	int first_sel = -1;
	for (int i = 0; i < n; i++) {
		char *p = xdg_uri_to_path(paths[i]);
		size_t l = strlen(p);
		while (l > 1 && p[l - 1] == '/')
			p[--l] = 0;
		const char *base = strrchr(p, '/');
		base = base ? base + 1 : p;
		for (int j = 0; j < ndirs; j++) {
			if (strcmp(dirs[j].name, base) != 0)
				continue;
			seladd(j);
			if (first_sel < 0)
				first_sel = j;
			break;
		}
		free(p);
	}
	if (first_sel >= 0) {
		if (first_sel < offset || first_sel >= offset + nlines) {
			offset = first_sel - nlines / 2;
			offset = scrollclamp(offset);
		}
	}
	redraw();
	jfm_raise_window();
}

void
jfm_show_properties(char **paths, int n)
{
	if (n <= 0)
		return;
	jfm_reveal(paths, n);
	char *first = xdg_uri_to_path(paths[0]);
	struct stat st;
	if (stat(first, &st) == 0) {
		char *mime = xdg_mime_of(first);
		char *time_str = ctime(&st.st_mtime);
		if (time_str) {
			char *nl = strchr(time_str, '\n');
			if (nl) *nl = 0;
		}
		const char *base = strrchr(first, '/');
		base = base ? base + 1 : first;
		char info[1024];
		snprintf(info, sizeof(info), "Size: %lld bytes | Modified: %s | Type: %s",
			 (long long)st.st_size, time_str ? time_str : "unknown", mime);
		alert(base, info);
		free(mime);
	}
	free(first);
}

int
main(int argc, char *argv[])
{
	setlocale(LC_CTYPE, "");

	enum {
		OPT_PORTAL_TOKEN = 1000,
		OPT_PORTAL_DEST,
		OPT_PORTAL_TITLE,
		OPT_PORTAL_DIR,
		OPT_PORTAL_ACCEPT,
		OPT_PORTAL_MODE,
		OPT_PORTAL_NAME,
		OPT_SAVE_AS,
		OPT_UPLOAD,
		OPT_ACCEPT,
	};
	static struct option options[] = {
		{ "select-file", no_argument, nil, 'f' },
		{ "choose-file", no_argument, nil, 'f' },
		{ "select-directory", no_argument, nil, 'd' },
		{ "select-multiple", no_argument, nil, 'm' },
		{ "multiple", no_argument, nil, 'm' },
		{ "save-file", no_argument, nil, 's' },
		{ "save-as", optional_argument, nil, OPT_SAVE_AS },
		{ "upload", no_argument, nil, OPT_UPLOAD },
		{ "accept", required_argument, nil, OPT_ACCEPT },
		{ "filter", required_argument, nil, OPT_ACCEPT },
		{ "select", no_argument, nil, 'S' },
		{ "reveal", no_argument, nil, 'S' },
		{ "print-uri", no_argument, nil, 'u' },
		{ "title", required_argument, nil, 't' },
		{ "directory", required_argument, nil, 'D' },
		{ "show-hidden", no_argument, nil, 'H' },
		{ "install-desktop", no_argument, nil, 'i' },
		{ "uninstall-desktop", no_argument, nil, 'I' },
		{ "set-default", no_argument, nil, 'b' },
		{ "unset-default", no_argument, nil, 'B' },
		{ "install-portal", no_argument, nil, 'P' },
		{ "uninstall-portal", no_argument, nil, 'p' },
		{ "print-mime", required_argument, nil, 'M' },
		{ "open-uri", required_argument, nil, 'O' },
		{ "portal-backend", no_argument, nil, 'R' },
		{ "version", no_argument, nil, 'v' },
		{ "help", no_argument, nil, 'h' },
		{ "portal-token", required_argument, nil, OPT_PORTAL_TOKEN },
		{ "portal-dest", required_argument, nil, OPT_PORTAL_DEST },
		{ "portal-title", required_argument, nil, OPT_PORTAL_TITLE },
		{ "portal-dir", required_argument, nil, OPT_PORTAL_DIR },
		{ "portal-accept", required_argument, nil, OPT_PORTAL_ACCEPT },
		{ "portal-mode", required_argument, nil, OPT_PORTAL_MODE },
		{ "portal-name", required_argument, nil, OPT_PORTAL_NAME },
		{ 0, 0, 0, 0 }
	};

	cfg = config_open();
	readhome();

	int mode = ModeNormal;
	int wanturi = 0;
	int sethidden = -1;
	int action = 0;
	int backend = 0;
	int select_mode = 0;
	char *title = nil;
	char *dirarg = nil;
	char *printmime = nil;
	char *openuri = nil;
	char *saveas = nil;

	int opt;
	while ((opt = getopt_long(argc, argv, "hvfmdsut:D:HiIbBpPM:O:RSa:", options, nil)) != -1) {
		switch (opt) {
		case 'f':
			mode = ModeOpen;
			break;
		case 'm':
			mode = ModeMultiple;
			break;
		case 'd':
			mode = ModeDirectory;
			break;
		case 's':
			mode = ModeSave;
			break;
		case OPT_SAVE_AS:
			mode = ModeSave;
			if (optarg)
				saveas = optarg;
			else if (optind < argc && argv[optind][0] != '-')
				saveas = argv[optind++];
			break;
		case OPT_UPLOAD:
			mode = ModeUpload;
			break;
		case OPT_ACCEPT:
		case OPT_PORTAL_ACCEPT:
			accepttypes = optarg;
			break;
		case 'S':
			select_mode = 1;
			break;
		case 'u':
			wanturi = 1;
			break;
		case 't':
			title = optarg;
			break;
		case 'D':
			dirarg = optarg;
			break;
		case 'H':
			sethidden = 1;
			break;
		case 'i':
			action = 1;
			break;
		case 'I':
			action = 2;
			break;
		case 'b':
			action = 5;
			break;
		case 'B':
			action = 6;
			break;
		case 'P':
			action = 3;
			break;
		case 'p':
			action = 4;
			break;
		case 'M':
			printmime = optarg;
			break;
		case 'O':
			openuri = optarg;
			break;
		case 'R':
			backend = 1;
			break;
		case 'v':
			printf("jfm %s\n", JFVERSION);
			return 0;
		case 'h':
			usage();
			return 0;
		case OPT_PORTAL_TOKEN:
			portaltoken = optarg;
			break;
		case OPT_PORTAL_DEST:
			portaldest = optarg;
			break;
		case OPT_PORTAL_TITLE:
			portaltitle = optarg;
			break;
		case OPT_PORTAL_DIR:
			portaldir = optarg;
			break;
		case OPT_PORTAL_MODE:
			portalmode = optarg;
			break;
		case OPT_PORTAL_NAME:
			portalname = optarg;
			break;
		default:
			usage();
			return 1;
		}
	}

	if (portalmode) {
		if (strcmp(portalmode, "save") == 0)
			mode = ModeSave;
		else if (strcmp(portalmode, "directory") == 0)
			mode = ModeDirectory;
		else if (strcmp(portalmode, "multiple") == 0)
			mode = ModeUpload;
		else
			mode = ModeOpen;
		if (!title)
			title = portaltitle;
		if (!dirarg)
			dirarg = portaldir;
	}

	if (mode == ModeSave && !saveas && portalname && *portalname)
		saveas = portalname;

	if (saveas) {
		char *expanded = nil;
		if (strncmp(saveas, "file://", 7) == 0)
			expanded = xdg_uri_to_path(saveas);
		else if (saveas[0] == '~')
			expanded = expandtilde(saveas);
		else
			expanded = strdup(saveas);
		size_t len = strlen(expanded);
		struct stat st;
		if ((len > 0 && expanded[len - 1] == '/') ||
		    (stat(expanded, &st) == 0 && S_ISDIR(st.st_mode))) {
			dirarg = expanded;
		} else {
			char *slash = strrchr(expanded, '/');
			if (slash) {
				*slash = 0;
				savename = strdup(slash + 1);
				dirarg = *expanded ? expanded : strdup("/");
				if (*expanded)
					expanded = nil;
			} else {
				savename = expanded;
				expanded = nil;
			}
			free(expanded);
		}
	}

	if (printmime) {
		char *mime = xdg_mime_of(printmime);
		printf("%s\n", mime);
		free(mime);
		return 0;
	}

	if (openuri) {
		xdg_open_uri(openuri);
		return 0;
	}

	if (backend) {
		dbus_portal_backend(argv[0]);
		return 0;
	}

	if (action) {
		char *exe = realpath(argv[0], nil);
		if (!exe)
			exe = strdup(argv[0]);
		switch (action) {
		case 1: {
			char *old = xdg_mime_default("inode/directory");
			if (old && strcmp(old, "jfm.desktop") != 0)
				config_set(cfg, "previous-default", old);
			else if (!old)
				config_set(cfg, "previous-default", "");
			free(old);
			xdg_install_desktop(exe);
			xdg_mimeapps_set_default("inode/directory", "jfm.desktop");
			config_save(cfg);
			printf("jfm: desktop entry and dbus service installed\n");
			break;
		}
		case 2: {
			const char *prev = config_get(cfg, "previous-default", "");
			if (prev && *prev)
				xdg_mimeapps_set_default("inode/directory", prev);
			else
				xdg_mimeapps_remove_default("inode/directory", "jfm.desktop");
			config_set(cfg, "previous-default", "");
			config_save(cfg);
			xdg_uninstall_desktop();
			printf("jfm: desktop entry and dbus service removed\n");
			break;
		}
		case 3:
			xdg_install_portal(exe);
			printf("jfm: portal backend installed\n");
			break;
		case 4:
			xdg_uninstall_portal();
			printf("jfm: portal backend removed\n");
			break;
		case 5: {
			char *old = xdg_mime_default("inode/directory");
			if (old && strcmp(old, "jfm.desktop") != 0)
				config_set(cfg, "previous-default", old);
			else if (!old)
				config_set(cfg, "previous-default", "");
			free(old);
			xdg_install_desktop(exe);
			xdg_mimeapps_set_default("inode/directory", "jfm.desktop");
			config_save(cfg);
			printf("jfm: default file manager for folders\n");
			break;
		}
		case 6: {
			const char *prev = config_get(cfg, "previous-default", "");
			if (prev && *prev)
				xdg_mimeapps_set_default("inode/directory", prev);
			else
				xdg_mimeapps_remove_default("inode/directory", "jfm.desktop");
			config_set(cfg, "previous-default", "");
			config_save(cfg);
			xdg_uninstall_desktop();
			printf("jfm: default file manager for folders removed\n");
			break;
		}
		}
		free(exe);
		return 0;
	}

	dpy = XOpenDisplay(nil);
	if (!dpy) {
		fprintf(stderr, "cannot open display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, scr);
	cmap = DefaultColormap(dpy, scr);
	depth = DefaultDepth(dpy, scr);

	int w = atoi(config_get(cfg, "width", "800"));
	int h = atoi(config_get(cfg, "height", "600"));
	if (w < 200)
		w = 800;
	if (h < 200)
		h = 600;
	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, w, h, 0,
				  BlackPixel(dpy, scr), WhitePixel(dpy, scr));
	XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | ButtonPressMask |
		     ButtonReleaseMask | PointerMotionMask | KeyPressMask);
	XMapWindow(dpy, win);

	gc = XCreateGC(dpy, win, 0, nil);
	loadfont(config_get(cfg, "font", "Tamzen-12"));

	XStoreName(dpy, win, title ? title : "jfm");

	Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmdelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wmdelete, 1);
	(void)wm_protocols;

	if (getcwd(path, sizeof(path)) == NULL)
		strcpy(path, "/");
	char *target_file_to_select = nil;
	if (argc > optind) {
		for (int i = optind; i < argc; i++) {
			char *raw = argv[i];
			char *expanded = nil;
			if (strncmp(raw, "file://", 7) == 0) {
				expanded = xdg_uri_to_path(raw);
			} else if (raw[0] == '~') {
				expanded = expandtilde(raw);
			} else {
				expanded = strdup(raw);
			}
			char *tmp = abspath(path, expanded);
			free(expanded);

			struct stat st;
			if (stat(tmp, &st) == 0 && !S_ISDIR(st.st_mode)) {
				char *slash = strrchr(tmp, '/');
				if (slash) {
					target_file_to_select = strdup(slash + 1);
					if (slash == tmp)
						strcpy(path, "/");
					else {
						*slash = 0;
						strncpy(path, tmp, sizeof(path) - 1);
						path[sizeof(path) - 1] = 0;
					}
				}
			} else if (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode) && select_mode) {
				char *slash = strrchr(tmp, '/');
				if (slash && slash != tmp) {
					target_file_to_select = strdup(slash + 1);
					*slash = 0;
					strncpy(path, tmp, sizeof(path) - 1);
					path[sizeof(path) - 1] = 0;
				} else {
					strncpy(path, tmp, sizeof(path) - 1);
					path[sizeof(path) - 1] = 0;
				}
			} else {
				strncpy(path, tmp, sizeof(path) - 1);
				path[sizeof(path) - 1] = 0;
			}
			free(tmp);
		}
	}
	if (dirarg) {
		char *expanded = nil;
		if (strncmp(dirarg, "file://", 7) == 0)
			expanded = xdg_uri_to_path(dirarg);
		else if (dirarg[0] == '~')
			expanded = expandtilde(dirarg);
		else
			expanded = strdup(dirarg);
		char *tmp = abspath(path, expanded);
		free(expanded);
		strncpy(path, tmp, sizeof(path) - 1);
		path[sizeof(path) - 1] = 0;
		free(tmp);
	}

	pickermode = mode;
	printuri = wanturi;
	showhidden = atoi(config_get(cfg, "showhidden", "0"));
	if (sethidden >= 0)
		showhidden = sethidden;

	loaddirs();
	histpush(path);
	if (target_file_to_select) {
		for (int j = 0; j < ndirs; j++) {
			if (strcmp(dirs[j].name, target_file_to_select) == 0) {
				seladd(j);
				if (j < offset || j >= offset + nlines) {
					offset = j - nlines / 2;
					offset = scrollclamp(offset);
				}
				break;
			}
		}
		free(target_file_to_select);
	}

	initcolors();

	evtresize(w, h);

	dbus_filemanager_init();

	g_unix_fd_add(ConnectionNumber(dpy), (GIOCondition)(G_IO_IN | G_IO_ERR | G_IO_HUP), x_fd_cb, nil);

	GMainLoop *loop = g_main_loop_new(nil, FALSE);
	g_main_loop_run(loop);
	return 0;
}
