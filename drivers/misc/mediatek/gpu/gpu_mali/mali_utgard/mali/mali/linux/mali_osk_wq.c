/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2015 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_osk_wq.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include <linux/slab.h> /* For memory allocation */
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/sched.h>

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_license.h"
#include "mali_kernel_linux.h"

typedef struct _mali_osk_wq_work_s {
	_mali_osk_wq_work_handler_t handler;
	void *data;
	mali_bool high_pri;
	struct work_struct work_handle;
} mali_osk_wq_work_object_t;

typedef struct _mali_osk_wq_delayed_work_s {
	_mali_osk_wq_work_handler_t handler;
	void *data;
	struct delayed_work work;
} mali_osk_wq_delayed_work_object_t;

#if MALI_LICENSE_IS_GPL
static struct workqueue_struct *mali_wq_normal = NULL;
static struct workqueue_struct *mali_wq_high = NULL;
#endif

static void _mali_osk_wq_work_func(struct work_struct *work);

_mali_osk_errcode_t _mali_osk_wq_init(void)
{
#if MALI_LICENSE_IS_GPL
	MALI_DEBUG_ASSERT(NULL == mali_wq_normal);
	MALI_DEBUG_ASSERT(NULL == mali_wq_high);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	mali_wq_normal = alloc_workqueue("mali", WQ_UNBOUND, 0);
	mali_wq_high = alloc_workqueue("mali_high_pri", WQ_HIGHPRI | WQ_UNBOUND, 0);
#else
	mali_wq_normal = create_workqueue("mali");
	mali_wq_high = create_workqueue("mali_high_pri");
#endif
	if (NULL == mali_wq_normal || NULL == mali_wq_high) {
		MALI_PRINT_ERROR(("Unable to create Mali workqueues\n"));

		if (mali_wq_normal) destroy_workqueue(mali_wq_normal);
		if (mali_wq_high)   destroy_workqueue(mali_wq_high);

		mali_wq_normal = NULL;
		mali_wq_high   = NULL;

		return _MALI_OSK_ERR_FAULT;
	}
#endif /* MALI_LICENSE_IS_GPL */

	return _MALI_OSK_ERR_OK;
}

void _mali_osk_wq_flush(void)
{
#if MALI_LICENSE_IS_GPL
	flush_workqueue(mali_wq_high);
	flush_workqueue(mali_wq_normal);
#else
	flush_scheduled_work();
#endif
}

void _mali_osk_wq_term(void)
{
#if MALI_LICENSE_IS_GPL
	MALI_DEBUG_ASSERT(NULL != mali_wq_normal);
	MALI_DEBUG_ASSERT(NULL != mali_wq_high);

	flush_workqueue(mali_wq_normal);
	destroy_workqueue(mali_wq_normal);

	flush_workqueue(mali_wq_high);
	destroy_workqueue(mali_wq_high);

	mali_wq_normal = NULL;
	mali_wq_high   = NULL;
#else
	flush_scheduled_work();
#endif
}

_mali_osk_wq_work_t *_mali_osk_wq_create_work(_mali_osk_wq_work_handler_t handler, void *data)
{
	mali_osk_wq_work_object_t *work = kmalloc(sizeof(mali_osk_wq_work_object_t), GFP_KERNEL);

	if (NULL == work) return NULL;

	work->handler = handler;
	work->data = data;
	work->high_pri = MALI_FALSE;

	INIT_WORK(&work->work_handle, _mali_osk_wq_work_func);

	return work;
}

_mali_osk_wq_work_t *_mali_osk_wq_create_work_high_pri(_mali_osk_wq_work_handler_t handler, void *data)
{
	mali_osk_wq_work_object_t *work = kmalloc(sizeof(mali_osk_wq_work_object_t), GFP_KERNEL);

	if (NULL == work) return NULL;

	work->handler = handler;
	work->data = data;
	work->high_pri = MALI_TRUE;

	INIT_WORK(&work->work_handle, _mali_osk_wq_work_func);

	return work;
}

void _mali_osk_wq_delete_work(_mali_osk_wq_work_t *work)
{
	mali_osk_wq_work_object_t *work_object = (mali_osk_wq_work_object_t *)work;
	_mali_osk_wq_flush();
	MALI_DEBUG_PRINT(2, ("_mali_osk_wq_delete_work: Delete work_object = %p\n", work_object));
	kfree(work_object);
}

void _mali_osk_wq_delete_work_nonflush(_mali_osk_wq_work_t *work)
{
	mali_osk_wq_work_object_t *work_object = (mali_osk_wq_work_object_t *)work;
	MALI_DEBUG_PRINT(2, ("_mali_osk_wq_delete_work_nonflush: Delete work_object = %p\n", work_object));
	kfree(work_object);
}

void _mali_osk_wq_schedule_work(_mali_osk_wq_work_t *work)
{
	mali_osk_wq_work_object_t *work_object = (mali_osk_wq_work_object_t *)work;
#if MALI_LICENSE_IS_GPL
	queue_work(mali_wq_normal, &work_object->work_handle);
#else
	schedule_work(&work_object->work_handle);
#endif
}

void _mali_osk_wq_schedule_work_high_pri(_mali_osk_wq_work_t *work)
{
	mali_osk_wq_work_object_t *work_object = (mali_osk_wq_work_object_t *)work;
#if MALI_LICENSE_IS_GPL
	queue_work(mali_wq_high, &work_object->work_handle);
#else
	schedule_work(&work_object->work_handle);
#endif
}

static void _mali_osk_wq_work_func(struct work_struct *work)
{
	mali_osk_wq_work_object_t *work_object;

	work_object = _MALI_OSK_CONTAINER_OF(work, mali_osk_wq_work_object_t, work_handle);

#if MALI_LICENSE_IS_GPL
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	/* We want highest Dynamic priority of the thread so that the Jobs depending
	** on this thread could be scheduled in time. Without this, this thread might
	** sometimes need to wait for some threads in user mode to finish its round-robin
	** time, causing *bubble* in the Mali pipeline. Thanks to the new implementation
	** of high-priority workqueue in new kernel, this only happens in older kernel.
	*/
	if (MALI_TRUE == work_object->high_pri) {
		set_user_nice(current, -19);
	}
#endif
#endif /* MALI_LICENSE_IS_GPL */

	work_object->handler(work_object->data);
}

static void _mali_osk_wq_delayed_work_func(struct work_struct *work)
{
	mali_osk_wq_delayed_work_object_t *work_object;

	work_object = _MALI_OSK_CONTAINER_OF(work, mali_osk_wq_delayed_work_object_t, work.work);
	work_object->handler(work_object->data);
}

mali_osk_wq_delayed_work_object_t *_mali_osk_wq_delayed_create_work(_mali_osk_wq_work_handler_t handler, void *data)
{
	mali_osk_wq_delayed_work_object_t *work = kmalloc(sizeof(mali_osk_wq_delayed_work_object_t), GFP_KERNEL);

	if (NULL == work) return NULL;

	work->handler = handler;
	work->data = data;

	INIT_DELAYED_WORK(&work->work, _mali_osk_wq_delayed_work_func);

	return work;
}

void _mali_osk_wq_delayed_delete_work_nonflush(_mali_osk_wq_delayed_work_t *work)
{
	mali_osk_wq_delayed_work_object_t *work_object = (mali_osk_wq_delayed_work_object_t *)work;
	MALI_DEBUG_PRINT(2, ("_mali_osk_wq_delayed_delete_work_nonflush: Delete work_object = %p\n", work_object));
	kfree(work_object);
}

void _mali_osk_wq_delayed_cancel_work_async(_mali_osk_wq_delayed_work_t *work)
{
	mali_osk_wq_delayed_work_object_t *work_object = (mali_osk_wq_delayed_work_object_t *)work;
	cancel_delayed_work(&work_object->work);
}

void _mali_osk_wq_delayed_cancel_work_sync(_mali_osk_wq_delayed_work_t *work)
{
	mali_osk_wq_delayed_work_object_t *work_object = (mali_osk_wq_delayed_work_object_t *)work;

	/*print work inform*/
	MALI_DEBUG_PRINT(2, ("Start print work_object = %p \n", work_object));
	MALI_DEBUG_PRINT(2, ("timeline->delayed_work->data = %p\n", work_object->data));
	MALI_DEBUG_PRINT(2, ("timeline->delayed_work->timer.entry.prev= %p\n", work_object->work.timer.entry.prev));
	MALI_DEBUG_PRINT(2, ("timeline->delayed_work->timer.entry.next= %p\n", work_object->work.timer.entry.next));
	MALI_DEBUG_PRINT(2, ("timeline->delayed_work->timer.expires= %ld\n", work_object->work.timer.expires));
	MALI_DEBUG_PRINT(2, ("End print\n"));
	
	cancel_delayed_work_sync(&work_object->work);
}

void _mali_osk_wq_delayed_schedule_work(_mali_osk_wq_delayed_work_t *work, u32 delay)
{
	mali_osk_wq_delayed_work_object_t *work_object = (mali_osk_wq_delayed_work_object_t *)work;

#if MALI_LICENSE_IS_GPL
	queue_delayed_work(mali_wq_normal, &work_object->work, delay);
#else
	schedule_delayed_work(&work_object->work, delay);
#endif

}