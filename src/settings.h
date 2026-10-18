/*
 * settings - update one "key = value" line in ps64.cfg, keeping the
 * rest of the file (comments included) as it was.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

/*
 * Set key to value: replaces an active "key = ..." line, else reuses a
 * commented-out "# key = ..." line, else appends. Any keys listed in
 * also_comment_out (NULL-terminated, may be NULL) are commented out.
 * The file is rewritten via path.tmp + rename. Returns 0 on success.
 */
int config_set(const char *path, const char *key, const char *value,
               const char *const *also_comment_out);

#endif
