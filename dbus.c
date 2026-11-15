#include "jfm.h"
#include "dbus.h"
#include "xdg.h"

#include <gio/gio.h>
#include <glib-unix.h>
#include <sys/wait.h>

static GDBusConnection *fmconn;
static GDBusConnection *portalconn;

typedef struct Pending Pending;
struct Pending {
	char *id;
	char *path;
	GPid pid;
	int done;
	GDBusMethodInvocation *invocation;
};

static Pending *pendings;
static int npendings;

int
dbus_session_available(void)
{
	GError *err = nil;
	GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, nil, &err);
	if (!c) {
		if (err)
			g_error_free(err);
		return 0;
	}
	return 1;
}

static char **
extract_uris(GVariant *parameters, gsize *n_out)
{
	*n_out = 0;
	if (!parameters)
		return nil;
	GVariant *child = nil;
	if (g_variant_is_of_type(parameters, G_VARIANT_TYPE("(ass)"))) {
		child = g_variant_get_child_value(parameters, 0);
	} else if (g_variant_is_of_type(parameters, G_VARIANT_TYPE("(as)"))) {
		child = g_variant_get_child_value(parameters, 0);
	} else if (g_variant_is_of_type(parameters, G_VARIANT_TYPE("as"))) {
		child = g_variant_ref(parameters);
	} else if (g_variant_n_children(parameters) > 0) {
		child = g_variant_get_child_value(parameters, 0);
		if (!g_variant_is_of_type(child, G_VARIANT_TYPE("as"))) {
			g_variant_unref(child);
			child = nil;
		}
	}
	if (!child)
		return nil;
	gsize n = 0;
	const gchar **items = g_variant_get_strv(child, &n);
	char **result = malloc(sizeof(char *) * (n + 1));
	for (gsize i = 0; i < n; i++)
		result[i] = strdup(items[i]);
	result[n] = nil;
	*n_out = n;
	g_free(items);
	g_variant_unref(child);
	return result;
}

static void
fm_method(GDBusConnection *conn, const char *sender, const char *object_path,
	  const char *interface_name, const char *method_name, GVariant *parameters,
	  GDBusMethodInvocation *invocation, gpointer user_data)
{
	if (strcmp(interface_name, "org.freedesktop.FileManager1") != 0) {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
						       "unknown method");
		return;
	}

	gsize n = 0;
	char **uris = extract_uris(parameters, &n);
	char **paths = nil;
	gsize np = 0;
	for (gsize i = 0; i < n; i++) {
		char *p = xdg_uri_to_path(uris[i]);
		paths = g_realloc(paths, sizeof(char *) * (np + 1));
		paths[np++] = p;
		free(uris[i]);
	}
	free(uris);

	if (strcmp(method_name, "ShowFolders") == 0) {
		if (np > 0)
			jfm_open_folder(paths[0]);
	} else if (strcmp(method_name, "ShowItems") == 0) {
		if (np > 0)
			jfm_reveal(paths, np);
	} else if (strcmp(method_name, "ShowItemProperties") == 0 ||
		   strcmp(method_name, "ShowProperties") == 0) {
		if (np > 0)
			jfm_show_properties(paths, np);
	} else {
		for (gsize i = 0; i < np; i++)
			free(paths[i]);
		g_free(paths);
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
						       "unknown method");
		return;
	}

	for (gsize i = 0; i < np; i++)
		free(paths[i]);
	g_free(paths);
	g_dbus_method_invocation_return_value(invocation, nil);
}

static const char fm_xml[] =
"<node>"
"  <interface name='org.freedesktop.FileManager1'>"
"    <method name='ShowFolders'>"
"      <arg type='as' name='URIs' direction='in'/>"
"      <arg type='s' name='StartupId' direction='in'/>"
"    </method>"
"    <method name='ShowItems'>"
"      <arg type='as' name='URIs' direction='in'/>"
"      <arg type='s' name='StartupId' direction='in'/>"
"    </method>"
"    <method name='ShowItemProperties'>"
"      <arg type='as' name='URIs' direction='in'/>"
"      <arg type='s' name='StartupId' direction='in'/>"
"    </method>"
"    <method name='ShowProperties'>"
"      <arg type='as' name='URIs' direction='in'/>"
"      <arg type='s' name='StartupId' direction='in'/>"
"    </method>"
"  </interface>"
"</node>";

static const GDBusInterfaceVTable fm_vtable = {
	fm_method, nil, nil, { 0 }
};

static void
fm_name_acquired(GDBusConnection *conn, const char *name, gpointer user_data)
{
}

static void
fm_name_lost(GDBusConnection *conn, const char *name, gpointer user_data)
{
}

static int
dbus_register_fm_object(GDBusConnection *conn)
{
	GError *err = nil;
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(fm_xml, &err);
	if (!info) {
		if (err)
			g_error_free(err);
		return -1;
	}
	g_dbus_connection_register_object(conn, "/org/freedesktop/FileManager1",
					  info->interfaces[0], &fm_vtable, nil, nil, &err);
	if (err) {
		g_error_free(err);
		g_dbus_node_info_unref(info);
		return -1;
	}
	g_dbus_node_info_unref(info);
	return 0;
}

int
dbus_filemanager_init(void)
{
	if (fmconn)
		return 0;
	GError *err = nil;
	fmconn = g_bus_get_sync(G_BUS_TYPE_SESSION, nil, &err);
	if (!fmconn) {
		if (err)
			g_error_free(err);
		return -1;
	}
	dbus_register_fm_object(fmconn);
	g_bus_own_name_on_connection(fmconn, "org.jfm.FileManager1", G_BUS_NAME_OWNER_FLAGS_NONE,
				     fm_name_acquired, fm_name_lost, nil, nil);
	g_bus_own_name_on_connection(fmconn, "org.freedesktop.FileManager1",
				     G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | G_BUS_NAME_OWNER_FLAGS_REPLACE,
				     fm_name_acquired, fm_name_lost, nil, nil);
	return 0;
}

int
dbus_dispatch(void)
{
	while (g_main_context_pending(nil))
		g_main_context_iteration(nil, FALSE);
	return 0;
}

static char *
strip_colon(const char *s)
{
	char *p = g_strdup(s ? s : "unknown");
	if (p[0] == ':')
		memmove(p, p + 1, strlen(p));
	return p;
}

static void
portal_reply(Pending *pending, int code, char **uris, int n)
{
	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
	if (uris && n > 0) {
		GVariantBuilder list;
		g_variant_builder_init(&list, G_VARIANT_TYPE("as"));
		for (int i = 0; i < n; i++)
			g_variant_builder_add(&list, "s", uris[i]);
		g_variant_builder_add(&builder, "{sv}", "uris", g_variant_builder_end(&list));
	}
	GVariant *reply_val = g_variant_new("(ua{sv})", (guint32)code, &builder);
	pending->done = 1;

	if (portalconn && pending->path) {
		GError *err = nil;
		g_dbus_connection_emit_signal(portalconn, nil, pending->path,
					      "org.freedesktop.impl.portal.Request",
					      "Response", reply_val, &err);
		if (err) {
			g_error_free(err);
			err = nil;
		}
		/*
		 * The result was delivered via the Response signal, so per the
		 * xdg-desktop-portal contract the method call itself must be
		 * answered with code 2 ("other") -- answering with the real
		 * result here as well would make the frontend process it twice.
		 */
		if (pending->invocation) {
			GVariantBuilder empty;
			g_variant_builder_init(&empty, G_VARIANT_TYPE("a{sv}"));
			g_dbus_method_invocation_return_value(pending->invocation,
				g_variant_new("(ua{sv})", (guint32)2, &empty));
			g_object_unref(pending->invocation);
			pending->invocation = nil;
		}
	} else if (pending->invocation) {
		g_dbus_method_invocation_return_value(pending->invocation, reply_val);
		g_object_unref(pending->invocation);
		pending->invocation = nil;
	}
}

static void
pendings_gc(void)
{
	int w = 0;
	for (int i = 0; i < npendings; i++) {
		if (!pendings[i].done) {
			if (w != i)
				pendings[w] = pendings[i];
			w++;
			continue;
		}
		g_free(pendings[i].id);
		g_free(pendings[i].path);
		if (pendings[i].invocation)
			g_object_unref(pendings[i].invocation);
	}
	npendings = w;
	if (npendings == 0) {
		free(pendings);
		pendings = nil;
	} else {
		pendings = g_realloc(pendings, sizeof(Pending) * npendings);
	}
}

static void
portal_finish(const char *id, char **uris, int n)
{
	if (!id)
		return;
	for (int i = 0; i < npendings; i++) {
		if (pendings[i].done || !pendings[i].id)
			continue;
		if (strcmp(pendings[i].id, id) != 0)
			continue;
		portal_reply(&pendings[i], n > 0 ? 0 : 1, uris, n);
		pendings_gc();
		return;
	}
}

static void
portal_result_method(GDBusConnection *conn, const char *sender, const char *object_path,
		      const char *interface_name, const char *method_name, GVariant *parameters,
		      GDBusMethodInvocation *invocation, gpointer user_data)
{
	if (strcmp(method_name, "Deliver") != 0) {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
						       "unknown method");
		return;
	}
	GVariant *tokvar = g_variant_get_child_value(parameters, 0);
	GVariant *strvar = g_variant_get_child_value(parameters, 1);
	const char *token = g_variant_get_string(tokvar, NULL);
	gsize n = 0;
	const char **items = g_variant_get_strv(strvar, &n);
	char **copy = nil;
	for (gsize i = 0; i < n; i++) {
		copy = g_realloc(copy, sizeof(char *) * (n + 1));
		copy[i] = g_strdup(items[i]);
	}
	if (n == 0) {
		copy = g_realloc(copy, sizeof(char *));
		copy[0] = nil;
	}
	portal_finish(token, copy, n);
	for (gsize i = 0; i < n; i++)
		free(copy[i]);
	g_free(copy);
	g_free(items);
	g_variant_unref(tokvar);
	g_variant_unref(strvar);
	g_dbus_method_invocation_return_value(invocation, nil);
}

static const char portal_result_xml[] =
"<node>"
"  <interface name='org.jfm.PortalResult'>"
"    <method name='Deliver'>"
"      <arg type='s' name='token' direction='in'/>"
"      <arg type='as' name='uris' direction='in'/>"
"    </method>"
"  </interface>"
"</node>";

static const GDBusInterfaceVTable portal_result_vtable = {
	portal_result_method, nil, nil, { 0 }
};

static void
portal_child_exit(GPid pid, gint status, gpointer data)
{
	g_spawn_close_pid(pid);
	for (int i = 0; i < npendings; i++) {
		if (pendings[i].done)
			continue;
		if (pendings[i].pid != pid)
			continue;
		portal_reply(&pendings[i], 1, nil, 0);
	}
	pendings_gc();
}

static char *
portal_spawn(const char *progname, const char *sender, const char *token, const char *title,
	     const char *folder, const char *accept, const char *mode_str, const char *current_name,
	     const char *basepath, GDBusMethodInvocation *invocation)
{
	char *senderid = strip_colon(sender);
	for (char *p = senderid; *p; p++)
		if (*p == '.')
			*p = '_';
	char *reqpath;
	if (basepath && *basepath)
		reqpath = g_strdup(basepath);
	else
		reqpath = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", senderid, token);
	g_free(senderid);

	const char *unique = g_dbus_connection_get_unique_name(portalconn);
	char *id = g_strdup_printf("%s.%d.%d", token, (int)getpid(), npendings);
	GPtrArray *args = g_ptr_array_new();
	g_ptr_array_add(args, g_strdup(progname));
	char *a = g_strdup_printf("--portal-token=%s", id);
	g_ptr_array_add(args, a);
	a = g_strdup_printf("--portal-dest=%s", unique ? unique : "");
	g_ptr_array_add(args, a);
	if (title && *title) {
		a = g_strdup_printf("--portal-title=%s", title);
		g_ptr_array_add(args, a);
	}
	if (folder && *folder) {
		a = g_strdup_printf("--portal-dir=%s", folder);
		g_ptr_array_add(args, a);
	}
	if (accept && *accept) {
		a = g_strdup_printf("--portal-accept=%s", accept);
		g_ptr_array_add(args, a);
	}
	a = g_strdup_printf("--portal-mode=%s", mode_str ? mode_str : "open");
	g_ptr_array_add(args, a);
	if (current_name && *current_name) {
		a = g_strdup_printf("--portal-name=%s", current_name);
		g_ptr_array_add(args, a);
	}
	g_ptr_array_add(args, nil);

	GPid pid = 0;
	GError *err = nil;
	char **argv = (char **)args->pdata;
	int spawned = g_spawn_async(nil, argv, nil, G_SPAWN_DO_NOT_REAP_CHILD, nil, nil, &pid, &err);
	if (!spawned && err) {
		fprintf(stderr, "jfm: cannot spawn file chooser: %s\n", err->message);
		g_error_free(err);
		err = nil;
	}
	if (spawned)
		g_child_watch_add(pid, portal_child_exit, nil);

	pendings = g_realloc(pendings, sizeof(Pending) * (npendings + 1));
	pendings[npendings].id = g_strdup(id);
	pendings[npendings].path = g_strdup(reqpath);
	pendings[npendings].pid = spawned ? pid : 0;
	pendings[npendings].done = 0;
	pendings[npendings].invocation = invocation ? g_object_ref(invocation) : nil;
	npendings++;
	if (!spawned)
		portal_reply(&pendings[npendings - 1], 2, nil, 0);

	g_ptr_array_free(args, TRUE);
	g_free(id);
	return reqpath;
}

static void
portal_filechooser_method(GDBusConnection *conn, const char *sender, const char *object_path,
			  const char *interface_name, const char *method_name, GVariant *parameters,
			  GDBusMethodInvocation *invocation, gpointer user_data)
{
	int save = strcmp(method_name, "SaveFile") == 0;
	if (!save && strcmp(method_name, "OpenFile") != 0) {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
						       "unknown method");
		return;
	}
	const char *optionspath = nil;
	const char *appid = nil;
	const char *parent = nil;
	const char *title = nil;
	GVariant *options = nil;
	g_variant_get(parameters, "(&o&s&s&s@a{sv})", &optionspath, &appid, &parent, &title, &options);

	char *token = nil;
	const char *tmp = nil;
	if (g_variant_lookup(options, "handle_token", "s", &tmp) && tmp)
		token = g_strdup(tmp);

	char *folder = nil;
	GVariant *cf_var = g_variant_lookup_value(options, "current_folder", NULL);
	if (cf_var) {
		if (g_variant_is_of_type(cf_var, G_VARIANT_TYPE_BYTESTRING)) {
			const char *cur = g_variant_get_bytestring(cf_var);
			if (cur && *cur)
				folder = xdg_uri_to_path(cur);
		} else if (g_variant_is_of_type(cf_var, G_VARIANT_TYPE_STRING)) {
			const char *cur = g_variant_get_string(cf_var, NULL);
			if (cur && *cur)
				folder = xdg_uri_to_path(cur);
		}
		g_variant_unref(cf_var);
	}

	const char *current_name = nil;
	g_variant_lookup(options, "current_name", "&s", &current_name);

	gboolean multiple = FALSE;
	g_variant_lookup(options, "multiple", "b", &multiple);

	gboolean directory = FALSE;
	g_variant_lookup(options, "directory", "b", &directory);

	GString *accept_str = g_string_new(nil);
	GVariant *filters_var = g_variant_lookup_value(options, "filters", G_VARIANT_TYPE("a(sa(us))"));
	if (filters_var) {
		GVariantIter iter;
		g_variant_iter_init(&iter, filters_var);
		const char *fname;
		GVariantIter *sub_iter;
		while (g_variant_iter_loop(&iter, "(&sa(us))", &fname, &sub_iter)) {
			guint32 ftype;
			const char *pattern;
			while (g_variant_iter_loop(sub_iter, "(u&s)", &ftype, &pattern)) {
				if (accept_str->len > 0)
					g_string_append_c(accept_str, ',');
				g_string_append(accept_str, pattern);
			}
		}
		g_variant_unref(filters_var);
	}

	char **types = nil;
	g_variant_lookup(options, "accept_types", "as", &types);
	if (types) {
		for (int i = 0; types[i]; i++) {
			if (accept_str->len > 0)
				g_string_append_c(accept_str, ',');
			g_string_append(accept_str, types[i]);
		}
		g_strfreev(types);
	}
	char *accept = accept_str->len > 0 ? g_string_free(accept_str, FALSE) : (g_string_free(accept_str, TRUE), nil);

	if (!token)
		token = g_strdup_printf("jfm%d", (int)getpid());

	const char *mode_str = "open";
	if (save)
		mode_str = "save";
	else if (directory)
		mode_str = "directory";
	else if (multiple)
		mode_str = "multiple";

	char *reqpath = portal_spawn((const char *)user_data, sender, token, title, folder, accept,
				      mode_str, current_name, optionspath, invocation);

	g_free(reqpath);
	g_free(token);
	g_free(accept);
	g_free(folder);
	g_variant_unref(options);
}

static const char portal_xml[] =
"<node>"
"  <interface name='org.freedesktop.impl.portal.FileChooser'>"
"    <method name='OpenFile'>"
"      <arg type='o' name='handle' direction='in'/>"
"      <arg type='s' name='app_id' direction='in'/>"
"      <arg type='s' name='parent_window' direction='in'/>"
"      <arg type='s' name='title' direction='in'/>"
"      <arg type='a{sv}' name='options' direction='in'/>"
"      <arg type='u' name='response' direction='out'/>"
"      <arg type='a{sv}' name='results' direction='out'/>"
"    </method>"
"    <method name='SaveFile'>"
"      <arg type='o' name='handle' direction='in'/>"
"      <arg type='s' name='app_id' direction='in'/>"
"      <arg type='s' name='parent_window' direction='in'/>"
"      <arg type='s' name='title' direction='in'/>"
"      <arg type='a{sv}' name='options' direction='in'/>"
"      <arg type='u' name='response' direction='out'/>"
"      <arg type='a{sv}' name='results' direction='out'/>"
"    </method>"
"  </interface>"
"</node>";

static const GDBusInterfaceVTable portal_vtable = {
	portal_filechooser_method, nil, nil, { 0 }
};

int
dbus_portal_backend(const char *progname)
{
	GError *err = nil;
	portalconn = g_bus_get_sync(G_BUS_TYPE_SESSION, nil, &err);
	if (!portalconn) {
		if (err) {
			fprintf(stderr, "jfm: cannot connect to session bus: %s\n", err->message);
			g_error_free(err);
		}
		return 1;
	}
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(portal_xml, &err);
	if (!info) {
		if (err) {
			fprintf(stderr, "jfm: %s\n", err->message);
			g_error_free(err);
		}
		return 1;
	}
	g_dbus_connection_register_object(portalconn, "/org/freedesktop/portal/desktop",
					  info->interfaces[0], &portal_vtable, (gpointer)progname, nil, &err);
	g_dbus_node_info_unref(info);
	if (err) {
		fprintf(stderr, "jfm: %s\n", err->message);
		g_error_free(err);
		return 1;
	}
	GDBusNodeInfo *rinfo = g_dbus_node_info_new_for_xml(portal_result_xml, &err);
	if (rinfo) {
		g_dbus_connection_register_object(portalconn, "/org/jfm/portal",
						  rinfo->interfaces[0], &portal_result_vtable, nil, nil, &err);
		g_dbus_node_info_unref(rinfo);
	}
	if (err) {
		g_error_free(err);
		err = nil;
	}

	guint owner = g_bus_own_name_on_connection(portalconn, "org.freedesktop.impl.portal.jfm",
						   G_BUS_NAME_OWNER_FLAGS_NONE, nil, nil, nil, nil);
	(void)owner;
	GMainLoop *loop = g_main_loop_new(nil, FALSE);
	g_main_loop_run(loop);
	return 0;
}

int
dbus_portal_deliver(const char *dest, const char *token, char **uris, int n)
{
	if (!dest || !*dest || !token)
		return -1;
	GError *err = nil;
	GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nil, &err);
	if (!conn) {
		if (err)
			g_error_free(err);
		return -1;
	}
	char *empty[] = { NULL };
	GVariant *reply = g_dbus_connection_call_sync(conn, dest, "/org/jfm/portal",
						       "org.jfm.PortalResult", "Deliver",
						       g_variant_new("(s^as)", token, uris ? uris : empty),
						       nil, G_DBUS_CALL_FLAGS_NONE, 5000, nil, &err);
	if (!reply) {
		if (err)
			g_error_free(err);
		return -1;
	}
	g_variant_unref(reply);
	return 0;
}
