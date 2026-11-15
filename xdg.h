#ifndef XDG_H
#define XDG_H

#include <sys/types.h>

char *xdg_home(void);
char *xdg_config_home(void);
char *xdg_data_home(void);
char *xdg_cache_home(void);
char *xdg_state_home(void);
char *xdg_runtime_dir(void);
char *xdg_config_dirs(void);
char *xdg_data_dirs(void);
char *xdg_path_join(const char *base, const char *rel);
char *xdg_config_path(const char *rel);
char *xdg_data_path(const char *rel);
char *xdg_cache_path(const char *rel);
char *xdg_state_path(const char *rel);
int xdg_mkdirp(const char *path, mode_t mode);
int xdg_mkdir_parents(const char *path);
char *xdg_user_dir(const char *name);
char *xdg_mime_of(const char *path);
char *xdg_mime_default(const char *mime);
char **xdg_mime_apps(const char *mime, int *n);
char *xdg_desktop_file(const char *desktopid);
char *xdg_desktop_field(const char *desktopid, const char *field);
char *xdg_desktop_exec(const char *desktopid);
int xdg_run_desktop(const char *desktopid, const char *arg);
int xdg_open_path(const char *path);
int xdg_open_uri(const char *uri);
char *xdg_path_to_uri(const char *path);
char *xdg_uri_to_path(const char *uri);
int xdg_mimeapps_set_default(const char *mime, const char *desktopid);
int xdg_mimeapps_remove_default(const char *mime, const char *desktopid);
int xdg_mimeapps_has_default(const char *mime, const char *desktopid);
char *xdg_desktop_template(void);
int xdg_install_desktop(const char *exe);
int xdg_uninstall_desktop(void);
int xdg_install_portal(const char *exe);
int xdg_uninstall_portal(void);

typedef struct Config Config;
Config *config_new(const char *file);
Config *config_open(void);
void config_free(Config *c);
const char *config_get(Config *c, const char *key, const char *def);
int config_has(Config *c, const char *key);
int config_set(Config *c, const char *key, const char *value);
int config_save(Config *c);

#endif
