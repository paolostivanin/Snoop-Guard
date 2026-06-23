#include <glib.h>
#include <glib/gstdio.h>
#include "daemon/main.h"
#include "daemon/sg-logging.h"
#include "daemon/sg-state.h"
#include "daemon/sg-policy.h"
#include "cli/sg-json.h"

static gchar *
write_config (const gchar *contents)
{
    gchar *path = NULL;
    gint fd = g_file_open_tmp ("snoop-guard-test-XXXXXX.ini", &path, NULL);
    g_assert_cmpint (fd, >=, 0);
    g_assert_true (g_close (fd, NULL));
    g_assert_true (g_file_set_contents (path, contents, -1, NULL));
    return path;
}

static void
test_config_valid (void)
{
    gchar *path = write_config (
        "[server]\ncheck_interval=2\nnotification_timeout=0\nlog_max_bytes=4096\n"
        "[policy]\nallow_list=zoom;teams;\ndeny_list=firefox;\n");
    GError *error = NULL;
    ConfigValues *cfg = load_config_file (path, TRUE, &error);
    g_assert_no_error (error);
    g_assert_nonnull (cfg);
    g_assert_cmpuint (cfg->check_interval, ==, 2);
    g_assert_cmpint (cfg->notification_timeout, ==, 0);
    g_assert_cmpstr (cfg->allow_list[1], ==, "teams");
    config_values_free (cfg);
    g_unlink (path);
    g_free (path);
}

static void
test_config_rejects_invalid (void)
{
    gchar *path = write_config ("[server]\ncheck_interval=0\n");
    GError *error = NULL;
    ConfigValues *cfg = load_config_file (path, TRUE, &error);
    g_assert_null (cfg);
    g_assert_error (error, g_quark_from_static_string ("snoop-guard-config-error"), 1);
    g_clear_error (&error);
    g_unlink (path);
    g_free (path);
}

static void
test_config_empty_lists (void)
{
    gchar *path = write_config (
        "[policy]\nallow_list=\ndeny_list=\nmic_allow_list=\nmic_deny_list=\n");
    GError *error = NULL;
    ConfigValues *cfg = load_config_file (path, TRUE, &error);
    g_assert_no_error (error);
    g_assert_nonnull (cfg);
    config_values_free (cfg);
    g_unlink (path);
    g_free (path);
}

static void
test_config_missing_explicit (void)
{
    gchar *path = g_build_filename (g_get_tmp_dir (),
                                    "snoop-guard-definitely-missing.ini", NULL);
    g_unlink (path);
    GError *error = NULL;
    ConfigValues *cfg = load_config_file (path, TRUE, &error);
    g_assert_null (cfg);
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_free (path);
}

static void
test_state_process_sets (void)
{
    gchar *log_path = g_build_filename (g_get_tmp_dir (), "snoop-guard-test.log", NULL);
    GError *error = NULL;
    g_assert_true (sg_log_init (log_path, 4096, &error));
    g_assert_no_error (error);
    sg_state_init ();
    gchar firefox[] = "firefox";
    gchar zoom[] = "zoom";
    gchar video[] = "/dev/video1";
    gchar *processes[] = { firefox, zoom, NULL };
    gchar *unknown[] = { video, NULL };
    g_assert_true (sg_state_set_webcam (
        TRUE, processes, unknown, "degraded", "attribution incomplete"));
    g_assert_cmpstr (sg_state.webcam_proc, ==, "firefox");
    g_assert_cmpstr (sg_state.webcam_processes[1], ==, "zoom");
    g_assert_false (sg_state_set_webcam (
        TRUE, processes, unknown, "degraded", "attribution incomplete"));
    sg_state_cleanup ();
    sg_log_uninit ();
    g_unlink (log_path);
    g_free (log_path);
}

static void
test_json_escape (void)
{
    gchar *escaped = sg_json_escape ("café\n\"camera\"");
    g_assert_cmpstr (escaped, ==, "café\\n\\\"camera\\\"");
    g_free (escaped);

    const gchar invalid[] = { 'x', (gchar) 0xff, '\0' };
    escaped = sg_json_escape (invalid);
    g_assert_true (g_utf8_validate (escaped, -1, NULL));
    g_free (escaped);
}

static void
test_policy_precedence (void)
{
    gchar firefox[] = "firefox";
    gchar zoom[] = "zoom";
    gchar *allow[] = { firefox, zoom, NULL };
    gchar *deny[] = { firefox, NULL };
    g_assert_true (sg_policy_should_notify (firefox, allow, deny));
    g_assert_false (sg_policy_should_notify (zoom, allow, deny));
    g_assert_true (sg_policy_should_notify ("other", allow, deny));
    g_assert_true (sg_policy_should_notify (NULL, allow, deny));
}

static void
test_log_rotation (void)
{
    gchar *dir = g_dir_make_tmp ("snoop-guard-log-test-XXXXXX", NULL);
    g_assert_nonnull (dir);
    gchar *path = g_build_filename (dir, "events.log", NULL);
    GError *error = NULL;
    g_assert_true (sg_log_init (path, 64, &error));
    g_assert_no_error (error);
    sg_log_event ("01234567890123456789012345678901234567890123456789");
    sg_log_event ("second event triggers rotation");
    gchar *backup = g_strconcat (path, ".1", NULL);
    g_assert_true (g_file_test (backup, G_FILE_TEST_IS_REGULAR));
    sg_log_uninit ();
    g_unlink (backup);
    g_unlink (path);
    g_rmdir (dir);
    g_free (backup);
    g_free (path);
    g_free (dir);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/config/valid", test_config_valid);
    g_test_add_func ("/config/reject-invalid", test_config_rejects_invalid);
    g_test_add_func ("/config/empty-lists", test_config_empty_lists);
    g_test_add_func ("/config/missing-explicit", test_config_missing_explicit);
    g_test_add_func ("/state/process-sets", test_state_process_sets);
    g_test_add_func ("/cli/json-escape", test_json_escape);
    g_test_add_func ("/policy/precedence", test_policy_precedence);
    g_test_add_func ("/logging/rotation", test_log_rotation);
    return g_test_run ();
}
