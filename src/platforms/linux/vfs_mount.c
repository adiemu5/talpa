
#include <linux/version.h>
#include "platforms/linux/vfs_mount.h"
#include "platforms/linux/log.h"
#include "platforms/linux/glue.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
 #define TALPA_REPLACE_MOUNT_STRUCT
 #define TALPA_MNT_NAMESPACE
#endif

#ifdef TALPA_REPLACE_MOUNT_STRUCT
struct talpa_mnt_pcp {
	int mnt_count;
	int mnt_writers;
};
 #if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
 struct talpa_replacement_mount_struct {
	struct list_head mnt_hash;
	struct talpa_replacement_mount_struct *mnt_parent;
	struct dentry *mnt_mountpoint;
	struct vfsmount mnt;
 # if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
    struct rcu_head mnt_rcu;
 # endif
 #ifdef CONFIG_SMP
	struct talpa_mnt_pcp __percpu *mnt_pcp;
 #else /* ! CONFIG_SMP */
	int mnt_count;
	int mnt_writers;
 #endif /* CONFIG_SMP */
	struct list_head mnt_mounts;	/* list of children, anchored here */
	struct list_head mnt_child;	/* and going through their mnt_child */
	struct list_head mnt_instance;	/* mount instance on sb->s_mounts */
	const char *mnt_devname;	/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	struct list_head mnt_share;	/* circular list of shared mounts */
	struct list_head mnt_slave_list;/* list of slave mounts */
	struct list_head mnt_slave;	/* slave list entry */
	struct mount *mnt_master;	/* slave is on master->mnt_slave_list */
	struct mnt_namespace *mnt_ns;	/* containing namespace */
};
 #else /* 3.3 - 3.5 */
 struct talpa_replacement_mount_struct {
	struct list_head mnt_hash;
	struct talpa_replacement_mount_struct *mnt_parent;
	struct dentry *mnt_mountpoint;
	struct vfsmount mnt;
 #ifdef CONFIG_SMP
	struct talpa_mnt_pcp __percpu *mnt_pcp;
	atomic_t mnt_longterm;		/* how many of the refs are longterm */
 #else
	int mnt_count;
	int mnt_writers;
 #endif
	struct list_head mnt_mounts;	/* list of children, anchored here */
	struct list_head mnt_child;	/* and going through their mnt_child */
	struct list_head mnt_instance;	/* mount instance on sb->s_mounts */
	const char *mnt_devname;	/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	struct list_head mnt_share;	/* circular list of shared mounts */
	struct list_head mnt_slave_list;/* list of slave mounts */
	struct list_head mnt_slave;	/* slave list entry */
	struct mount *mnt_master;	/* slave is on master->mnt_slave_list */
	struct mnt_namespace *mnt_ns;	/* containing namespace */
};
 #endif /* 3.6 */
static inline struct talpa_replacement_mount_struct *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct talpa_replacement_mount_struct, mnt);
}

#endif /* TALPA_REPLACE_MOUNT_STRUCT */

struct vfsmount* getParent(struct vfsmount* mnt)
{
#ifndef TALPA_REPLACE_MOUNT_STRUCT
    return mnt->mnt_parent;
#else
    struct talpa_replacement_mount_struct *realmnt = real_mount(mnt);
    return &(realmnt->mnt_parent->mnt);
#endif
}

/**
 * @return borrowed reference to device name
 */
const char *getDeviceName(struct vfsmount* mnt)
{
#ifndef TALPA_REPLACE_MOUNT_STRUCT
    return mnt->mnt_devname;
#else
    struct talpa_replacement_mount_struct *realmnt = real_mount(mnt);
    return realmnt->mnt_devname;
#endif
}

#ifdef TALPA_MNT_NAMESPACE
struct mnt_namespace *getNamespaceInfo(struct vfsmount* mnt)
{
    struct talpa_replacement_mount_struct *realmnt = real_mount(mnt);
    /* TODO: remove this debug trap */
    if (NULL == realmnt )
    {
        critical("getNamespaceInfo: real_mount(mnt) returned NULL");
        return NULL;
    }
    return realmnt->mnt_ns;
}
#endif

/**
 */
struct dentry *getVfsMountPoint(struct vfsmount* mnt)
{
#ifndef TALPA_REPLACE_MOUNT_STRUCT
    return mnt->mnt_mountpoint;
#else
    struct talpa_replacement_mount_struct *realmnt = real_mount(mnt);
    dbg("%p", realmnt->mnt_mountpoint);
    return realmnt->mnt_mountpoint;
#endif
}


#ifndef TALPA_REPLACE_MOUNT_STRUCT
int iterateFilesystems(struct vfsmount* root, int (*callback) (struct vfsmount* mnt, unsigned long flags, bool fromMount))
{
    struct vfsmount *mnt, *nextmnt, *prevmnt;
    struct list_head *nexthead = NULL;
    int ret;
    unsigned m_seq = 1;


    mnt = mntget(root); /* Extra reference count for the loop */
    do
    {
        dbg("VFSMNT: 0x%p (at 0x%p), sb: 0x%p, dev: %s, flags: 0x%lx, fs: %s", mnt, getParent(mnt), mnt->mnt_sb, mnt->mnt_devname, mnt->mnt_sb->s_flags, mnt->mnt_sb->s_type->name);

        ret = callback(mnt, mnt->mnt_sb->s_flags, false);
        if (ret)
        {
            break;
        }

        talpa_vfsmount_lock(&m_seq); /* locks dcache_lock on 2.4 */

        /* Go down the tree for a child if there is one */
        if ( !list_empty(&mnt->mnt_mounts) )
        {
            nextmnt = list_entry(mnt->mnt_mounts.next, struct vfsmount, mnt_child);
        }
        else
        {
            nextmnt = mnt;
            /* If no children, go up until we found some. Abort on root. */
            while ( nextmnt != getParent(nextmnt) )
            {
                nexthead = nextmnt->mnt_child.next;
                /* Take next child if available */
                if ( nexthead != &getParent(nextmnt)->mnt_mounts )
                {
                    break;
                }
                /* Otherwise go up the tree */
                nextmnt = getParent(nextmnt);
            }

            /* Abort if we are at the root */
            if ( nextmnt == getParent(nextmnt) )
            {
                talpa_vfsmount_unlock(&m_seq); /* unlocks dcache_lock on 2.4 */
                mntput(mnt);
                break;
            }

            /* Take next mount from the list */
            nextmnt = list_entry(nexthead, struct vfsmount, mnt_child);
        }

        mntget(nextmnt);
        prevmnt = mnt;
        mnt = nextmnt;
        talpa_vfsmount_unlock(&m_seq); /* unlocks dcache_lock on 2.4 */
        mntput(prevmnt);
    } while (mnt);

    /* Don't mntput root as we didn't take a reference for ourselves */

    return ret;
}
#else /* TALPA_REPLACE_MOUNT_STRUCT */
int iterateFilesystems(struct vfsmount* root, int (*callback) (struct vfsmount* mnt, unsigned long flags, bool fromMount))
{
    struct talpa_replacement_mount_struct *mnt, *nextmnt, *prevmnt;
    struct list_head *nexthead = NULL;
    int ret;
    unsigned m_seq = 1;


    mnt = real_mount(root);
    mntget(&mnt->mnt); /* Take extra reference count for the loop */
    do
    {
        struct vfsmount* vfsmnt = &mnt->mnt;
        dbg("VFSMNT: 0x%p (at 0x%p), sb: 0x%p, dev: %s, flags: 0x%lx, fs: %s", mnt, mnt->mnt_parent,
                vfsmnt->mnt_sb, mnt->mnt_devname, vfsmnt->mnt_sb->s_flags, vfsmnt->mnt_sb->s_type->name);

        ret = callback(vfsmnt, vfsmnt->mnt_sb->s_flags, false);
        if (ret)
        {
            break;
        }

        talpa_vfsmount_lock(&m_seq); /* locks dcache_lock on 2.4 */

        /* Go down the tree for a child if there is one */
        if ( !list_empty(&mnt->mnt_mounts) )
        {
            nextmnt = list_entry(mnt->mnt_mounts.next, struct talpa_replacement_mount_struct, mnt_child);
        }
        else
        {
            nextmnt = mnt;
            /* If no children, go up until we found some. Abort on root. */
            while ( nextmnt != nextmnt->mnt_parent )
            {
                nexthead = nextmnt->mnt_child.next;
                /* Take next child if available */
                if ( nexthead != &nextmnt->mnt_parent->mnt_mounts )
                {
                    break;
                }
                /* Otherwise go up the tree */
                nextmnt = nextmnt->mnt_parent;
            }

            /* Abort if we are at the root */
            if ( nextmnt == nextmnt->mnt_parent )
            {
                talpa_vfsmount_unlock(&m_seq); /* unlocks dcache_lock on 2.4 */
                mntput(&mnt->mnt);
                break;
            }

            /* Take next mount from the list */
            nextmnt = list_entry(nexthead, struct talpa_replacement_mount_struct, mnt_child);
        }

        mntget(&nextmnt->mnt);
        prevmnt = mnt;
        mnt = nextmnt;
        talpa_vfsmount_unlock(&m_seq); /* unlocks dcache_lock on 2.4 */
        mntput(&prevmnt->mnt);
    } while (mnt);

    return ret;
}
#endif /* TALPA_REPLACE_MOUNT_STRUCT */
