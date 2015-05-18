#include <stdio.h>
#include <locale.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <bundle.h>
#include <context.h>
#include <install.h>
#include <manifest.h>
#include <mount.h>

#include "common.h"

typedef struct {
	gchar *tmpdir;
} InstallFixture;

#define SLOT_SIZE (10*1024*1024)

static void install_fixture_set_up(InstallFixture *fixture,
		gconstpointer user_data)
{
	gchar *configpath;
	gchar *certpath;
	gchar *keypath;
	gchar *capath;

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
	g_assert_nonnull(fixture->tmpdir);
	g_print("bundle tmpdir: %s\n", fixture->tmpdir);

	g_assert(test_mkdir_relative(fixture->tmpdir, "content", 0777) == 0);
	g_assert(test_mkdir_relative(fixture->tmpdir, "mount", 0777) == 0);
	g_assert(test_mkdir_relative(fixture->tmpdir, "images", 0777) == 0);
	g_assert(test_mkdir_relative(fixture->tmpdir, "openssl-ca", 0777) == 0);

	/* copy system config to temp dir*/
	configpath = g_build_filename(fixture->tmpdir, "system.conf", NULL);
	g_assert_nonnull(configpath);
	g_assert_true(test_copy_file("test/test.conf", configpath));
	r_context_conf()->configpath = g_strdup(configpath);

	/* copy cert */
	certpath = g_build_filename(fixture->tmpdir, "openssl-ca/release-1.cert.pem", NULL);
	g_assert_nonnull(certpath);
	g_assert_true(test_copy_file("test/openssl-ca/rel/release-1.cert.pem", certpath));
	r_context_conf()->certpath = g_strdup(certpath);

	/* copy key */
	keypath = g_build_filename(fixture->tmpdir, "openssl-ca/release-1.pem", NULL);
	g_assert_nonnull(keypath);
	g_assert_true(test_copy_file("test/openssl-ca/rel/private/release-1.pem", keypath));
	r_context_conf()->keypath = g_strdup(keypath);

	/* copy ca */
	capath = g_build_filename(fixture->tmpdir, "openssl-ca/dev-ca.pem", NULL);
	g_assert_nonnull(capath);
	g_assert_true(test_copy_file("test/openssl-ca/dev-ca.pem", capath));

	/* Setup pseudo devices */
	g_assert(test_prepare_dummy_file(fixture->tmpdir, "images/rootfs-1",
				         SLOT_SIZE, "/dev/zero") == 0);
	g_assert(test_prepare_dummy_file(fixture->tmpdir, "images/appfs-1",
					 SLOT_SIZE, "/dev/zero") == 0);
	g_assert_true(test_make_filesystem(fixture->tmpdir, "images/rootfs-1"));
	g_assert_true(test_make_filesystem(fixture->tmpdir, "images/appfs-1"));

	/* Make images user-writable */
	test_make_slot_user_writable(fixture->tmpdir, "images/rootfs-1");
	test_make_slot_user_writable(fixture->tmpdir, "images/appfs-1");
	
	/* Set dummy bootname provider */
	set_bootname_provider(test_bootname_provider);

	g_free(configpath);
	g_free(certpath);
	g_free(keypath);
	g_free(capath);
}

static void install_fixture_set_up_bundle(InstallFixture *fixture,
		gconstpointer user_data) {
	gchar *contentdir;
	gchar *bundlepath;

	install_fixture_set_up(fixture, user_data);

	contentdir = g_build_filename(fixture->tmpdir, "content", NULL);
	bundlepath = g_build_filename(fixture->tmpdir, "bundle.raucb", NULL);

	/* Setup bundle content */
	g_assert(test_prepare_dummy_file(fixture->tmpdir, "content/rootfs.img",
					 SLOT_SIZE, "/dev/zero") == 0);
	g_assert(test_prepare_dummy_file(fixture->tmpdir, "content/appfs.img",
					 SLOT_SIZE, "/dev/zero") == 0);
	g_assert_true(test_make_filesystem(fixture->tmpdir, "content/rootfs.img"));
	g_assert_true(test_make_filesystem(fixture->tmpdir, "content/appfs.img"));
	g_assert(test_prepare_manifest_file(fixture->tmpdir, "content/manifest.raucm") == 0);

	/* Make images user-writable */
	test_make_slot_user_writable(fixture->tmpdir, "content/rootfs.img");
	test_make_slot_user_writable(fixture->tmpdir, "content/appfs.img");

	/* Update checksums in manifest */
	g_assert_true(update_manifest(contentdir, FALSE));

	/* Create bundle */
	g_assert_true(create_bundle(bundlepath, contentdir));

	g_free(bundlepath);
	g_free(contentdir);
}

static void rename_manifest(const gchar *contentdir, const gchar *targetname) {
	gchar *manifestpath1 = g_strconcat(contentdir,
			"/manifest.raucm", NULL);
	gchar *manifestpath2 = g_strconcat(contentdir,
			"/", targetname, ".raucm", NULL);
	gchar *signaturepath1 = g_strconcat(contentdir,
			"/manifest.raucm.sig", NULL);
	gchar *signaturepath2 = g_strconcat(contentdir,
			"/", targetname, ".raucm.sig", NULL);

	g_assert(g_rename(manifestpath1, manifestpath2) == 0);
	g_assert(g_rename(signaturepath1, signaturepath2) == 0);

	g_free(manifestpath1);
	g_free(manifestpath2);
	g_free(signaturepath1);
	g_free(signaturepath2);
}

static void install_fixture_set_up_network(InstallFixture *fixture,
		gconstpointer user_data) {
	RaucManifest *rm = g_new0(RaucManifest, 1);
	RaucFile *files;
	gchar *contentdir;
	gchar *manifestpath;

	install_fixture_set_up(fixture, user_data);

	contentdir = g_build_filename(fixture->tmpdir, "content", NULL);
	manifestpath = g_build_filename(fixture->tmpdir, "content/manifest.raucm", NULL);

	/* Setup bundle content */
	g_assert(test_prepare_dummy_file(fixture->tmpdir, "content/vmlinuz-1",
					 64*1024, "/dev/urandom") == 0);
	g_assert(test_prepare_dummy_file(fixture->tmpdir, "content/vmlinuz-2",
					 64*1024, "/dev/urandom") == 0);
	g_assert(test_prepare_dummy_file(fixture->tmpdir, "content/initramfs-1",
					 32*1024, "/dev/urandom") == 0);

	/* Prepare manifest */
	rm->update_compatible = g_strdup("Test Config");
	rm->update_version = g_strdup("2011.03-2");

	files = g_new0(RaucFile, 2);

	files[0].slotclass = g_strdup("rootfs");
	files[0].filename = g_strdup("vmlinuz-1");
	files[0].destname = g_strdup("vmlinuz");
	rm->files = g_list_append(rm->files, &files[0]);

	files[1].slotclass = g_strdup("rootfs");
	files[1].filename = g_strdup("initramfs-1");
	files[1].destname = g_strdup("initramfs");
	rm->files = g_list_append(rm->files, &files[1]);

	/* Create signed manifest */
	g_assert_true(save_manifest_file(manifestpath, rm));
	g_assert_true(update_manifest(contentdir, TRUE));
	rename_manifest(contentdir, "manifest-1");

	/* Modify manifest vmlinuz-1 -> vmlinuz-2 */
	files[0].filename = g_strdup("vmlinuz-2");
	g_assert_true(save_manifest_file(manifestpath, rm));
	g_assert_true(update_manifest(contentdir, TRUE));
	rename_manifest(contentdir, "manifest-2");

	/* Modify manifest (no initramfs) */
	files[0].filename = g_strdup("vmlinuz-2");
	rm->files = g_list_remove(rm->files, &files[1]);
	g_assert_true(save_manifest_file(manifestpath, rm));
	g_assert_true(update_manifest(contentdir, TRUE));
	rename_manifest(contentdir, "manifest-3");

	free_manifest(rm);
	g_free(manifestpath);
	g_free(contentdir);
}

static void install_fixture_tear_down(InstallFixture *fixture,
		gconstpointer user_data)
{
	//test_umount(fixture->tmpdir, "mount/bundle");
}

static void install_test_bootname(void)
{
	g_assert_nonnull(get_cmdline_bootname());
}

static void install_test_target(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucManifest *rm;
	GHashTable *tgrp;

	g_assert_true(load_manifest_file("test/manifest.raucm", &rm));

	set_bootname_provider(test_bootname_provider);
	g_assert_true(determine_slot_states());

	g_assert_nonnull(r_context()->config);
	g_assert_nonnull(r_context()->config->slots);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "rescue.0"))->state, ==, ST_INACTIVE);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "rootfs.0"))->state, ==, ST_ACTIVE);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "rootfs.1"))->state, ==, ST_INACTIVE);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "appfs.0"))->state, ==, ST_ACTIVE);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "appfs.1"))->state, ==, ST_INACTIVE);

	tgrp = determine_target_install_group(rm);

	g_assert_true(g_hash_table_contains(tgrp, "rootfs"));
	g_assert_true(g_hash_table_contains(tgrp, "appfs"));
	g_assert_cmpstr(g_hash_table_lookup(tgrp, "rootfs"), ==, "rootfs.1");
	g_assert_cmpstr(g_hash_table_lookup(tgrp, "appfs"), ==, "appfs.1");
	g_assert_cmpint(g_hash_table_size(tgrp), ==, 2);
}

static void install_test_bundle(InstallFixture *fixture,
		gconstpointer user_data)
{
	gchar *bundlepath;
	gchar* mountdir;

	/* Set mount path to current temp dir */
	mountdir = g_build_filename(fixture->tmpdir, "mount", NULL);
	g_assert_nonnull(mountdir);
	r_context_conf()->mountprefix = g_strdup(mountdir);
	r_context();

	bundlepath = g_build_filename(fixture->tmpdir, "bundle.raucb", NULL);
	g_assert_nonnull(bundlepath);

	g_assert_true(do_install_bundle(bundlepath));
}

static void install_test_network(InstallFixture *fixture,
		gconstpointer user_data)
{
	gchar *manifesturl;
	gchar *mountdir;

	/* Set mount path to current temp dir */
	mountdir = g_build_filename(fixture->tmpdir, "mount", NULL);
	g_assert_nonnull(mountdir);
	r_context_conf()->mountprefix = g_strdup(mountdir);
	r_context();

	manifesturl = g_strconcat("file://", fixture->tmpdir,
				  "/content/manifest-1.raucm", NULL);
	g_assert_true(do_install_network(manifesturl));
	g_free(manifesturl);

	manifesturl = g_strconcat("file://", fixture->tmpdir,
				  "/content/manifest-2.raucm", NULL);
	g_assert_true(do_install_network(manifesturl));
	g_free(manifesturl);

	manifesturl = g_strconcat("file://", fixture->tmpdir,
				  "/content/manifest-3.raucm", NULL);
	g_assert_true(do_install_network(manifesturl));
	g_free(manifesturl);
}

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");

	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/install/bootname", install_test_bootname);

	g_test_add("/install/target", InstallFixture, NULL,
		   install_fixture_set_up, install_test_target,
		   install_fixture_tear_down);

	g_test_add("/install/bundle", InstallFixture, NULL,
		   install_fixture_set_up_bundle, install_test_bundle,
		   install_fixture_tear_down);

	g_test_add("/install/network", InstallFixture, NULL,
		   install_fixture_set_up_network, install_test_network,
		   install_fixture_tear_down);

	return g_test_run();
}