#ifdef CONFIG_KSU_SUSFS
extern u32 susfs_zygote_sid;
extern u32 susfs_zygote_next_sid;

static inline void ksu_handle_extra_susfs_work(void)
{
	extern struct work_struct susfs_extra_works;

	if (work_pending(&susfs_extra_works))
		return;

	schedule_work(&susfs_extra_works);
}

// Common tail for a zygote (or zygote_next) spawned process that is about
// to become an app (isolated service, or a normal app uid): mark it for
// SUSFS so SUS_PATH/SUS_MOUNT/SUS_KSTAT apply to it, matching the intent
// of the generic SUSFS-for-KernelSU patch's handle_zygote*_setresuid().
static int handle_zygote_family_setresuid(struct cred *new, const struct cred *old, uid_t new_uid, bool is_zygote_next)
{
	if (is_isolated_process(new_uid)) {
		susfs_set_current_proc_no_su();
		susfs_set_current_proc_umounted();
		if (is_zygote_next)
			susfs_set_current_proc_umounted_for_zygote_next();
		goto do_susfs_work;
	}

	if (unlikely(is_uid_manager(new_uid))) {
		pr_info("install fd for manager: %d\n", new_uid);
		ksu_install_fd();
		return 0;
	}

	if (likely(is_appuid(new_uid) && ksu_uid_should_umount(new_uid))) {
		susfs_set_current_proc_no_su();
		susfs_set_current_proc_umounted();
		if (is_zygote_next)
			susfs_set_current_proc_umounted_for_zygote_next();
		goto do_susfs_work;
	}

	if (ksu_is_allow_uid_for_current(new_uid)) {
		disable_seccomp();
		set_thread_flag(TIF_KSU_MANAGED); // sucompat fast-path
		return 0;
	}

	// Not umounted, and root not allowed for this uid either.
	susfs_set_current_proc_no_su();
	return 0;

do_susfs_work:
	if (!is_zygote_next) {
		// zygote_next-spawned processes are still in the init mount
		// namespace at this point, so umounting here would be wrong;
		// only umount for classic zygote-spawned processes.
		ksu_handle_umount(new, old);
	}
	ksu_handle_extra_susfs_work();
	return 0;
}
#endif /* CONFIG_KSU_SUSFS */

static __always_inline void ksu_handle_setresuid_cred(struct cred *new, const struct cred *old)
{
	if (!new || !old)
		return;

	uid_t new_uid = ksu_get_uid_t(new->uid);
	uid_t old_uid = ksu_get_uid_t(old->uid);

	// old process is not root, ignore it.
	if (unlikely(!!old_uid))
		return;

	if (IS_ENABLED(CONFIG_KSU_DEBUG))
		pr_info("handle_setresuid from %d to %d\n", old_uid, new_uid);

#ifdef CONFIG_KSU_SUSFS
	// We are only interested in processes spawned by zygote or
	// zygote_next here; everything else (manager, adbd, su itself, ...)
	// falls through to the existing generic handling below unchanged.
	if (susfs_is_sid_equal(old, susfs_zygote_sid)) {
		handle_zygote_family_setresuid(new, old, new_uid, false);
		return;
	}
	if (susfs_is_sid_equal(old, susfs_zygote_next_sid)) {
		handle_zygote_family_setresuid(new, old, new_uid, true);
		return;
	}
#endif

	// we dont have those new fancy things upstream has
	// lets just do the original thing where we disable seccomp
	if (unlikely(is_uid_manager(new_uid)))
		goto install_ksu_fd;

	if (ksu_is_allow_uid_for_current(new_uid))
		goto kill_seccomp;

	// Handle kernel umount
	ksu_handle_umount(new, old);
	return;

install_ksu_fd:
	pr_info("install fd for manager: %d\n", new_uid);
	ksu_install_fd();

kill_seccomp:
	disable_seccomp();
	set_thread_flag(TIF_KSU_MANAGED); // sucompat fast-path
	return;
}
