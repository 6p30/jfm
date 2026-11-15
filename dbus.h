#ifndef DBUS_H
#define DBUS_H

int dbus_filemanager_init(void);
int dbus_dispatch(void);
int dbus_session_available(void);
int dbus_portal_backend(const char *progname);
int dbus_portal_deliver(const char *dest, const char *token, char **uris, int n);

#endif
