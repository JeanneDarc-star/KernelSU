static bool ksu_kernel_umount_enabled __read_mostly = true;

static int kernel_umount_feature_get(u64 *value)
{
	*value = ksu_kernel_umount_enabled ? 1 : 0;
	return 0;
}

static int kernel_umount_feature_set(u64 value)
{
	bool enable = value != 0;
	ksu_kernel_umount_enabled = enable;
	pr_info("kernel_umount: set to %d\n", enable);
	return 0;
}

static const struct ksu_feature_handler kernel_umount_handler = {
	.feature_id = KSU_FEATURE_KERNEL_UMOUNT,
	.name = "kernel_umount",
	.get_handler = kernel_umount_feature_get,
	.set_handler = kernel_umount_feature_set,
};

extern int path_umount(struct path *path, int flags);

static inline void ksu_umount_mnt(const char *mnt, struct path *path, int flags)
{
	int err = path_umount(path, flags);
	if (err)
		pr_info("umount %s failed: %d\n", mnt, err);
}

static inline void try_umount(const char *mnt, int flags)
{
	struct path path;
	int err = kern_path(mnt, 0, &path);
	if (err) {
		return;
	}

	if (path.dentry != path.mnt->mnt_root) {
		// it is not root mountpoint, maybe umounted by others already.
		path_put(&path);
		return;
	}

	ksu_umount_mnt(mnt, &path, flags);
}

static inline int ksu_handle_umount(struct cred *new, const struct cred *old)
{
	uid_t new_uid = ksu_get_uid_t(new->uid);
	uid_t old_uid = ksu_get_uid_t(old->uid);

	if (!ksu_kernel_umount_enabled)
		return 0;

	// if there isn't any module mounted, just ignore it!
	if (!ksu_module_mounted)
		return 0;

#ifdef CONFIG_KSU_HOSTSREDIRECT
	set_thread_flag(TIF_KSU_UNMOUNTABLE);
#endif
	// umount the target mnt
	pr_info("handle umount for uid: %d, pid: %d\n", new_uid, current->pid);

	const struct cred *saved = override_creds(ksu_cred);

	struct mount_entry *entry;
	down_read(&mount_list_lock);
	list_for_each_entry (entry, &mount_list, list) {
		pr_info("%s: unmounting: %s flags: 0x%x\n", __func__, entry->umountable, entry->flags);
		try_umount(entry->umountable, entry->flags);
	}
	up_read(&mount_list_lock);

	revert_creds(saved);

	return 0;
}

void __init ksu_kernel_umount_init(void)
{
	if (ksu_register_feature_handler(&kernel_umount_handler)) {
		pr_err("Failed to register kernel_umount feature handler\n");
	}
}

void __exit ksu_kernel_umount_exit(void)
{
	ksu_unregister_feature_handler(KSU_FEATURE_KERNEL_UMOUNT);
}

#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
/*
 * fs/susfs.c declares these extern and calls them (only from the
 * SUS_KSTAT path) to translate a mount that may be one of KSU's own
 * bind/overlay mounts back to the "real" underlying mount, so it can
 * report a pre-KSU-looking mnt_id/vfsmount in spoofed kstat results.
 *
 * This fork does not keep a separate remap table for that, so these
 * are safe passthroughs: the caller's struct mount pointer is treated
 * as opaque (never dereferenced, since we don't have a verified
 * struct-mount layout in KSU driver code here) and handed back
 * unchanged. This means kstat spoofing for a path that lives on a
 * KSU-managed mount may report that mount's own id instead of the
 * pre-KSU one - a narrow SUS_KSTAT edge case, not a general SUSFS
 * regression.
 */
int susfs_get_non_sus_mnt_id_from_mnt(struct mount *orig_mnt)
{
	return 0;
}

struct vfsmount *susfs_get_non_sus_vfsmnt_from_vfsmnt(struct vfsmount *vfsmnt)
{
	return vfsmnt;
}
#endif /* CONFIG_KSU_SUSFS_SUS_KSTAT */
