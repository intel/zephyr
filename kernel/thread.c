/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Kernel thread support
 *
 * This module provides general purpose thread support.
 */

#include <kernel.h>
#include <spinlock.h>
#include <sys/math_extras.h>
#include <sys_clock.h>
#include <ksched.h>
#include <wait_q.h>
#include <syscall_handler.h>
#include <kernel_internal.h>
#include <kswap.h>
#include <init.h>
#include <tracing/tracing.h>
#include <string.h>
#include <stdbool.h>
#include <irq_offload.h>
#include <sys/check.h>
#include <random/rand32.h>
#include <sys/atomic.h>
#include <logging/log.h>
LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

#ifdef CONFIG_THREAD_RUNTIME_STATS
k_thread_runtime_stats_t threads_runtime_stats;
#endif

#ifdef CONFIG_THREAD_MONITOR
/* This lock protects the linked list of active threads; i.e. the
 * initial _kernel.threads pointer and the linked list made up of
 * thread->next_thread (until NULL)
 */
static struct k_spinlock z_thread_monitor_lock;
#endif /* CONFIG_THREAD_MONITOR */

#define _FOREACH_STATIC_THREAD(thread_data)              \
	STRUCT_SECTION_FOREACH(_static_thread_data, thread_data)

void k_thread_foreach(k_thread_user_cb_t user_cb, void *user_data)
{
#if defined(CONFIG_THREAD_MONITOR)
	struct k_thread *thread;
	k_spinlock_key_t key;

	__ASSERT(user_cb != NULL, "user_cb can not be NULL");

	/*
	 * Lock is needed to make sure that the _kernel.threads is not being
	 * modified by the user_cb either directly or indirectly.
	 * The indirect ways are through calling k_thread_create and
	 * k_thread_abort from user_cb.
	 */
	key = k_spin_lock(&z_thread_monitor_lock);

	SYS_PORT_TRACING_FUNC_ENTER(k_thread, foreach);

	for (thread = _kernel.threads; thread; thread = thread->next_thread) {
		user_cb(thread, user_data);
	}

	SYS_PORT_TRACING_FUNC_EXIT(k_thread, foreach);

	k_spin_unlock(&z_thread_monitor_lock, key);
#endif
}

void k_thread_foreach_unlocked(k_thread_user_cb_t user_cb, void *user_data)
{
#if defined(CONFIG_THREAD_MONITOR)
	struct k_thread *thread;
	k_spinlock_key_t key;

	__ASSERT(user_cb != NULL, "user_cb can not be NULL");

	key = k_spin_lock(&z_thread_monitor_lock);

	SYS_PORT_TRACING_FUNC_ENTER(k_thread, foreach_unlocked);

	for (thread = _kernel.threads; thread; thread = thread->next_thread) {
		k_spin_unlock(&z_thread_monitor_lock, key);
		user_cb(thread, user_data);
		key = k_spin_lock(&z_thread_monitor_lock);
	}

	SYS_PORT_TRACING_FUNC_EXIT(k_thread, foreach_unlocked);

	k_spin_unlock(&z_thread_monitor_lock, key);
#endif
}

bool k_is_in_isr(void)
{
	return arch_is_in_isr();
}

/*
 * This function tags the current thread as essential to system operation.
 * Exceptions raised by this thread will be treated as a fatal system error.
 */
void z_thread_essential_set(void)
{
	_current->base.user_options |= K_ESSENTIAL;
}

/*
 * This function tags the current thread as not essential to system operation.
 * Exceptions raised by this thread may be recoverable.
 * (This is the default tag for a thread.)
 */
void z_thread_essential_clear(void)
{
	_current->base.user_options &= ~K_ESSENTIAL;
}

/*
 * This routine indicates if the current thread is an essential system thread.
 *
 * Returns true if current thread is essential, false if it is not.
 */
bool z_is_thread_essential(void)
{
	return (_current->base.user_options & K_ESSENTIAL) == K_ESSENTIAL;
}

#ifdef CONFIG_THREAD_CUSTOM_DATA
void z_impl_k_thread_custom_data_set(void *value)
{
	_current->custom_data = value;
}

#ifdef CONFIG_USERSPACE
static inline void z_vrfy_k_thread_custom_data_set(void *data)
{
	z_impl_k_thread_custom_data_set(data);
}
#include <syscalls/k_thread_custom_data_set_mrsh.c>
#endif

void *z_impl_k_thread_custom_data_get(void)
{
	return _current->custom_data;
}

#ifdef CONFIG_USERSPACE
static inline void *z_vrfy_k_thread_custom_data_get(void)
{
	return z_impl_k_thread_custom_data_get();
}
#include <syscalls/k_thread_custom_data_get_mrsh.c>

#endif /* CONFIG_USERSPACE */
#endif /* CONFIG_THREAD_CUSTOM_DATA */

#if defined(CONFIG_THREAD_MONITOR)
/*
 * Remove a thread from the kernel's list of active threads.
 */
void z_thread_monitor_exit(struct k_thread *thread)
{
	k_spinlock_key_t key = k_spin_lock(&z_thread_monitor_lock);

	if (thread == _kernel.threads) {
		_kernel.threads = _kernel.threads->next_thread;
	} else {
		struct k_thread *prev_thread;

		prev_thread = _kernel.threads;
		while ((prev_thread != NULL) &&
			(thread != prev_thread->next_thread)) {
			prev_thread = prev_thread->next_thread;
		}
		if (prev_thread != NULL) {
			prev_thread->next_thread = thread->next_thread;
		}
	}

	k_spin_unlock(&z_thread_monitor_lock, key);
}
#endif

int z_impl_k_thread_name_set(struct k_thread *thread, const char *value)
{
#ifdef CONFIG_THREAD_NAME
	if (thread == NULL) {
		thread = _current;
	}

	strncpy(thread->name, value, CONFIG_THREAD_MAX_NAME_LEN);
	thread->name[CONFIG_THREAD_MAX_NAME_LEN - 1] = '\0';

	SYS_PORT_TRACING_OBJ_FUNC(k_thread, name_set, thread, 0);

	return 0;
#else
	ARG_UNUSED(thread);
	ARG_UNUSED(value);

	SYS_PORT_TRACING_OBJ_FUNC(k_thread, name_set, thread, -ENOSYS);

	return -ENOSYS;
#endif /* CONFIG_THREAD_NAME */
}

#ifdef CONFIG_USERSPACE
static inline int z_vrfy_k_thread_name_set(struct k_thread *thread, const char *str)
{
#ifdef CONFIG_THREAD_NAME
	char name[CONFIG_THREAD_MAX_NAME_LEN];

	if (thread != NULL) {
		if (Z_SYSCALL_OBJ(thread, K_OBJ_THREAD) != 0) {
			return -EINVAL;
		}
	}

	/* In theory we could copy directly into thread->name, but
	 * the current z_vrfy / z_impl split does not provide a
	 * means of doing so.
	 */
	if (z_user_string_copy(name, (char *)str, sizeof(name)) != 0) {
		return -EFAULT;
	}

	return z_impl_k_thread_name_set(thread, name);
#else
	return -ENOSYS;
#endif /* CONFIG_THREAD_NAME */
}
#include <syscalls/k_thread_name_set_mrsh.c>
#endif /* CONFIG_USERSPACE */

const char *k_thread_name_get(struct k_thread *thread)
{
#ifdef CONFIG_THREAD_NAME
	return (const char *)thread->name;
#else
	ARG_UNUSED(thread);
	return NULL;
#endif /* CONFIG_THREAD_NAME */
}

int z_impl_k_thread_name_copy(k_tid_t thread, char *buf, size_t size)
{
#ifdef CONFIG_THREAD_NAME
	strncpy(buf, thread->name, size);
	return 0;
#else
	ARG_UNUSED(thread);
	ARG_UNUSED(buf);
	ARG_UNUSED(size);
	return -ENOSYS;
#endif /* CONFIG_THREAD_NAME */
}

const char *k_thread_state_str(k_tid_t thread_id)
{
	switch (thread_id->base.thread_state) {
	case 0:
		return "";
	case _THREAD_DUMMY:
		return "dummy";
	case _THREAD_PENDING:
		return "pending";
	case _THREAD_PRESTART:
		return "prestart";
	case _THREAD_DEAD:
		return "dead";
	case _THREAD_SUSPENDED:
		return "suspended";
	case _THREAD_ABORTING:
		return "aborting";
	case _THREAD_QUEUED:
		return "queued";
	default:
	/* Add a break, some day when another case gets added at the end,
	 * this bit of defensive programming will be useful
	 */
		break;
	}
	return "unknown";
}

#ifdef CONFIG_USERSPACE
static inline int z_vrfy_k_thread_name_copy(k_tid_t thread,
					    char *buf, size_t size)
{
#ifdef CONFIG_THREAD_NAME
	size_t len;
	struct z_object *ko = z_object_find(thread);

	/* Special case: we allow reading the names of initialized threads
	 * even if we don't have permission on them
	 */
	if (thread == NULL || ko->type != K_OBJ_THREAD ||
	    (ko->flags & K_OBJ_FLAG_INITIALIZED) == 0) {
		return -EINVAL;
	}
	if (Z_SYSCALL_MEMORY_WRITE(buf, size) != 0) {
		return -EFAULT;
	}
	len = strlen(thread->name);
	if (len + 1 > size) {
		return -ENOSPC;
	}

	return z_user_to_copy((void *)buf, thread->name, len + 1);
#else
	ARG_UNUSED(thread);
	ARG_UNUSED(buf);
	ARG_UNUSED(size);
	return -ENOSYS;
#endif /* CONFIG_THREAD_NAME */
}
#include <syscalls/k_thread_name_copy_mrsh.c>
#endif /* CONFIG_USERSPACE */


#ifdef CONFIG_MULTITHREADING
#ifdef CONFIG_STACK_SENTINEL
/* Check that the stack sentinel is still present
 *
 * The stack sentinel feature writes a magic value to the lowest 4 bytes of
 * the thread's stack when the thread is initialized. This value gets checked
 * in a few places:
 *
 * 1) In k_yield() if the current thread is not swapped out
 * 2) After servicing a non-nested interrupt
 * 3) In z_swap(), check the sentinel in the outgoing thread
 *
 * Item 2 requires support in arch/ code.
 *
 * If the check fails, the thread will be terminated appropriately through
 * the system fatal error handler.
 */
void z_check_stack_sentinel(void)
{
	uint32_t *stack;

	if ((_current->base.thread_state & _THREAD_DUMMY) != 0) {
		return;
	}

	stack = (uint32_t *)_current->stack_info.start;
	if (*stack != STACK_SENTINEL) {
		/* Restore it so further checks don't trigger this same error */
		*stack = STACK_SENTINEL;
		z_except_reason(K_ERR_STACK_CHK_FAIL);
	}
}
#endif /* CONFIG_STACK_SENTINEL */

void z_impl_k_thread_start(struct k_thread *thread)
{
	SYS_PORT_TRACING_OBJ_FUNC(k_thread, start, thread);

	z_sched_start(thread);
}

#ifdef CONFIG_USERSPACE
static inline void z_vrfy_k_thread_start(struct k_thread *thread)
{
	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));
	return z_impl_k_thread_start(thread);
}
#include <syscalls/k_thread_start_mrsh.c>
#endif
#endif

#ifdef CONFIG_MULTITHREADING
static void schedule_new_thread(struct k_thread *thread, k_timeout_t delay)
{
#ifdef CONFIG_SYS_CLOCK_EXISTS
	if (K_TIMEOUT_EQ(delay, K_NO_WAIT)) {
		k_thread_start(thread);
	} else {
		z_add_thread_timeout(thread, delay);
	}
#else
	ARG_UNUSED(delay);
	k_thread_start(thread);
#endif
}
#endif

#if CONFIG_STACK_POINTER_RANDOM
int z_stack_adjust_initialized;

static size_t random_offset(size_t stack_size)
{
	size_t random_val;

	if (!z_stack_adjust_initialized) {
		z_early_boot_rand_get((uint8_t *)&random_val, sizeof(random_val));
	} else {
		sys_rand_get((uint8_t *)&random_val, sizeof(random_val));
	}

	/* Don't need to worry about alignment of the size here,
	 * arch_new_thread() is required to do it.
	 *
	 * FIXME: Not the best way to get a random number in a range.
	 * See #6493
	 */
	const size_t fuzz = random_val % CONFIG_STACK_POINTER_RANDOM;

	if (unlikely(fuzz * 2 > stack_size)) {
		return 0;
	}

	return fuzz;
}
#if defined(CONFIG_STACK_GROWS_UP)
	/* This is so rare not bothering for now */
#error "Stack pointer randomization not implemented for upward growing stacks"
#endif /* CONFIG_STACK_GROWS_UP */
#endif /* CONFIG_STACK_POINTER_RANDOM */

static char *setup_thread_stack(struct k_thread *new_thread,
				k_thread_stack_t *stack, size_t stack_size)
{
	size_t stack_obj_size, stack_buf_size;
	char *stack_ptr, *stack_buf_start;
	size_t delta = 0;

#ifdef CONFIG_USERSPACE
	if (z_stack_is_user_capable(stack)) {
		stack_obj_size = Z_THREAD_STACK_SIZE_ADJUST(stack_size);
		stack_buf_start = Z_THREAD_STACK_BUFFER(stack);
		stack_buf_size = stack_obj_size - K_THREAD_STACK_RESERVED;
	} else
#endif
	{
		/* Object cannot host a user mode thread */
		stack_obj_size = Z_KERNEL_STACK_SIZE_ADJUST(stack_size);
		stack_buf_start = Z_KERNEL_STACK_BUFFER(stack);
		stack_buf_size = stack_obj_size - K_KERNEL_STACK_RESERVED;
	}

	/* Initial stack pointer at the high end of the stack object, may
	 * be reduced later in this function by TLS or random offset
	 */
	stack_ptr = (char *)stack + stack_obj_size;

	LOG_DBG("stack %p for thread %p: obj_size=%zu buf_start=%p "
		" buf_size %zu stack_ptr=%p",
		stack, new_thread, stack_obj_size, stack_buf_start,
		stack_buf_size, stack_ptr);

#ifdef CONFIG_INIT_STACKS
	memset(stack_buf_start, 0xaa, stack_buf_size);
#endif
#ifdef CONFIG_STACK_SENTINEL
	/* Put the stack sentinel at the lowest 4 bytes of the stack area.
	 * We periodically check that it's still present and kill the thread
	 * if it isn't.
	 */
	*((uint32_t *)stack_buf_start) = STACK_SENTINEL;
#endif /* CONFIG_STACK_SENTINEL */
#ifdef CONFIG_THREAD_LOCAL_STORAGE
	/* TLS is always last within the stack buffer */
	delta += arch_tls_stack_setup(new_thread, stack_ptr);
#endif /* CONFIG_THREAD_LOCAL_STORAGE */
#ifdef CONFIG_THREAD_USERSPACE_LOCAL_DATA
	size_t tls_size = sizeof(struct _thread_userspace_local_data);

	/* reserve space on highest memory of stack buffer for local data */
	delta += tls_size;
	new_thread->userspace_local_data =
		(struct _thread_userspace_local_data *)(stack_ptr - delta);
#endif
#if CONFIG_STACK_POINTER_RANDOM
	delta += random_offset(stack_buf_size);
#endif
	delta = ROUND_UP(delta, ARCH_STACK_PTR_ALIGN);
#ifdef CONFIG_THREAD_STACK_INFO
	/* Initial values. Arches which implement MPU guards that "borrow"
	 * memory from the stack buffer (not tracked in K_THREAD_STACK_RESERVED)
	 * will need to appropriately update this.
	 *
	 * The bounds tracked here correspond to the area of the stack object
	 * that the thread can access, which includes TLS.
	 */
	new_thread->stack_info.start = (uintptr_t)stack_buf_start;
	new_thread->stack_info.size = stack_buf_size;
	new_thread->stack_info.delta = delta;
#endif
	stack_ptr -= delta;

	return stack_ptr;
}

#define THREAD_COOKIE	0x1337C0D3

/*
 * The provided stack_size value is presumed to be either the result of
 * K_THREAD_STACK_SIZEOF(stack), or the size value passed to the instance
 * of K_THREAD_STACK_DEFINE() which defined 'stack'.
 */
char *z_setup_new_thread(struct k_thread *new_thread,
			 k_thread_stack_t *stack, size_t stack_size,
			 k_thread_entry_t entry,
			 void *p1, void *p2, void *p3,
			 int prio, uint32_t options, const char *name)
{
	char *stack_ptr;

	Z_ASSERT_VALID_PRIO(prio, entry);

#ifdef CONFIG_USERSPACE
	__ASSERT((options & K_USER) == 0U || z_stack_is_user_capable(stack),
		 "user thread %p with kernel-only stack %p",
		 new_thread, stack);
	z_object_init(new_thread);
	z_object_init(stack);
	new_thread->stack_obj = stack;
	new_thread->syscall_frame = NULL;

	/* Any given thread has access to itself */
	k_object_access_grant(new_thread, new_thread);
#endif
	z_waitq_init(&new_thread->join_queue);

	/* Initialize various struct k_thread members */
	z_init_thread_base(&new_thread->base, prio, _THREAD_PRESTART, options);
	stack_ptr = setup_thread_stack(new_thread, stack, stack_size);

#ifdef CONFIG_KERNEL_COHERENCE
	/* Check that the thread object is safe, but that the stack is
	 * still cached!
	 */
	__ASSERT_NO_MSG(arch_mem_coherent(new_thread));
	__ASSERT_NO_MSG(!arch_mem_coherent(stack));
#endif

	arch_new_thread(new_thread, stack, stack_ptr, entry, p1, p2, p3);

	/* static threads overwrite it afterwards with real value */
	new_thread->init_data = NULL;

#ifdef CONFIG_USE_SWITCH
	/* switch_handle must be non-null except when inside z_swap()
	 * for synchronization reasons.  Historically some notional
	 * USE_SWITCH architectures have actually ignored the field
	 */
	__ASSERT(new_thread->switch_handle != NULL,
		 "arch layer failed to initialize switch_handle");
#endif
#ifdef CONFIG_THREAD_CUSTOM_DATA
	/* Initialize custom data field (value is opaque to kernel) */
	new_thread->custom_data = NULL;
#endif
#ifdef CONFIG_THREAD_MONITOR
	new_thread->entry.pEntry = entry;
	new_thread->entry.parameter1 = p1;
	new_thread->entry.parameter2 = p2;
	new_thread->entry.parameter3 = p3;

	k_spinlock_key_t key = k_spin_lock(&z_thread_monitor_lock);

	new_thread->next_thread = _kernel.threads;
	_kernel.threads = new_thread;
	k_spin_unlock(&z_thread_monitor_lock, key);
#endif
#ifdef CONFIG_THREAD_NAME
	if (name != NULL) {
		strncpy(new_thread->name, name,
			CONFIG_THREAD_MAX_NAME_LEN - 1);
		/* Ensure NULL termination, truncate if longer */
		new_thread->name[CONFIG_THREAD_MAX_NAME_LEN - 1] = '\0';
	} else {
		new_thread->name[0] = '\0';
	}
#endif
#ifdef CONFIG_SCHED_CPU_MASK
	new_thread->base.cpu_mask = -1;
#endif
#ifdef CONFIG_ARCH_HAS_CUSTOM_SWAP_TO_MAIN
	/* _current may be null if the dummy thread is not used */
	if (!_current) {
		new_thread->resource_pool = NULL;
		return stack_ptr;
	}
#endif
#ifdef CONFIG_USERSPACE
	z_mem_domain_init_thread(new_thread);

	if ((options & K_INHERIT_PERMS) != 0U) {
		z_thread_perms_inherit(_current, new_thread);
	}
#endif
#ifdef CONFIG_SCHED_DEADLINE
	new_thread->base.prio_deadline = 0;
#endif
	new_thread->resource_pool = _current->resource_pool;

	SYS_PORT_TRACING_OBJ_FUNC(k_thread, create, new_thread);

#ifdef CONFIG_THREAD_RUNTIME_STATS
	memset(&new_thread->rt_stats, 0, sizeof(new_thread->rt_stats));
#endif

	return stack_ptr;
}

#ifdef CONFIG_MULTITHREADING
k_tid_t z_impl_k_thread_create(struct k_thread *new_thread,
			      k_thread_stack_t *stack,
			      size_t stack_size, k_thread_entry_t entry,
			      void *p1, void *p2, void *p3,
			      int prio, uint32_t options, k_timeout_t delay)
{
	__ASSERT(!arch_is_in_isr(), "Threads may not be created in ISRs");

	z_setup_new_thread(new_thread, stack, stack_size, entry, p1, p2, p3,
			  prio, options, NULL);

	if (!K_TIMEOUT_EQ(delay, K_FOREVER)) {
		schedule_new_thread(new_thread, delay);
	}

	return new_thread;
}


#ifdef CONFIG_USERSPACE
bool z_stack_is_user_capable(k_thread_stack_t *stack)
{
	return z_object_find(stack) != NULL;
}

k_tid_t z_vrfy_k_thread_create(struct k_thread *new_thread,
			       k_thread_stack_t *stack,
			       size_t stack_size, k_thread_entry_t entry,
			       void *p1, void *p2, void *p3,
			       int prio, uint32_t options, k_timeout_t delay)
{
	size_t total_size, stack_obj_size;
	struct z_object *stack_object;

	/* The thread and stack objects *must* be in an uninitialized state */
	Z_OOPS(Z_SYSCALL_OBJ_NEVER_INIT(new_thread, K_OBJ_THREAD));

	/* No need to check z_stack_is_user_capable(), it won't be in the
	 * object table if it isn't
	 */
	stack_object = z_object_find(stack);
	Z_OOPS(Z_SYSCALL_VERIFY_MSG(z_obj_validation_check(stack_object, stack,
						K_OBJ_THREAD_STACK_ELEMENT,
						_OBJ_INIT_FALSE) == 0,
				    "bad stack object"));

	/* Verify that the stack size passed in is OK by computing the total
	 * size and comparing it with the size value in the object metadata
	 */
	Z_OOPS(Z_SYSCALL_VERIFY_MSG(!size_add_overflow(K_THREAD_STACK_RESERVED,
						       stack_size, &total_size),
				    "stack size overflow (%zu+%zu)",
				    stack_size,
				    K_THREAD_STACK_RESERVED));

	/* Testing less-than-or-equal since additional room may have been
	 * allocated for alignment constraints
	 */
#ifdef CONFIG_GEN_PRIV_STACKS
	stack_obj_size = stack_object->data.stack_data->size;
#else
	stack_obj_size = stack_object->data.stack_size;
#endif
	Z_OOPS(Z_SYSCALL_VERIFY_MSG(total_size <= stack_obj_size,
				    "stack size %zu is too big, max is %zu",
				    total_size, stack_obj_size));

	/* User threads may only create other user threads and they can't
	 * be marked as essential
	 */
	Z_OOPS(Z_SYSCALL_VERIFY(options & K_USER));
	Z_OOPS(Z_SYSCALL_VERIFY(!(options & K_ESSENTIAL)));

	/* Check validity of prio argument; must be the same or worse priority
	 * than the caller
	 */
	Z_OOPS(Z_SYSCALL_VERIFY(_is_valid_prio(prio, NULL)));
	Z_OOPS(Z_SYSCALL_VERIFY(z_is_prio_lower_or_equal(prio,
							_current->base.prio)));

	z_setup_new_thread(new_thread, stack, stack_size,
			   entry, p1, p2, p3, prio, options, NULL);

	if (!K_TIMEOUT_EQ(delay, K_FOREVER)) {
		schedule_new_thread(new_thread, delay);
	}

	return new_thread;
}
#include <syscalls/k_thread_create_mrsh.c>
#endif /* CONFIG_USERSPACE */
#endif /* CONFIG_MULTITHREADING */

#ifdef CONFIG_MULTITHREADING
#ifdef CONFIG_USERSPACE

static void grant_static_access(void)
{
	STRUCT_SECTION_FOREACH(z_object_assignment, pos) {
		for (int i = 0; pos->objects[i] != NULL; i++) {
			k_object_access_grant(pos->objects[i],
					      pos->thread);
		}
	}
}
#endif /* CONFIG_USERSPACE */

void z_init_static_threads(void)
{
	_FOREACH_STATIC_THREAD(thread_data) {
		z_setup_new_thread(
			thread_data->init_thread,
			thread_data->init_stack,
			thread_data->init_stack_size,
			thread_data->init_entry,
			thread_data->init_p1,
			thread_data->init_p2,
			thread_data->init_p3,
			thread_data->init_prio,
			thread_data->init_options,
			thread_data->init_name);

		thread_data->init_thread->init_data = thread_data;
	}

#ifdef CONFIG_USERSPACE
	grant_static_access();
#endif

	/*
	 * Non-legacy static threads may be started immediately or
	 * after a previously specified delay. Even though the
	 * scheduler is locked, ticks can still be delivered and
	 * processed. Take a sched lock to prevent them from running
	 * until they are all started.
	 *
	 * Note that static threads defined using the legacy API have a
	 * delay of K_FOREVER.
	 */
	k_sched_lock();
	_FOREACH_STATIC_THREAD(thread_data) {
		if (thread_data->init_delay != K_TICKS_FOREVER) {
			schedule_new_thread(thread_data->init_thread,
					    K_MSEC(thread_data->init_delay));
		}
	}
	k_sched_unlock();
}
#endif

void z_init_thread_base(struct _thread_base *thread_base, int priority,
		       uint32_t initial_state, unsigned int options)
{
	/* k_q_node is initialized upon first insertion in a list */
	thread_base->pended_on = NULL;
	thread_base->user_options = (uint8_t)options;
	thread_base->thread_state = (uint8_t)initial_state;

	thread_base->prio = priority;

	thread_base->sched_locked = 0U;

#ifdef CONFIG_SMP
	thread_base->is_idle = 0;
#endif

	/* swap_data does not need to be initialized */

	z_init_thread_timeout(thread_base);
}

FUNC_NORETURN void k_thread_user_mode_enter(k_thread_entry_t entry,
					    void *p1, void *p2, void *p3)
{
	SYS_PORT_TRACING_FUNC(k_thread, user_mode_enter);

	_current->base.user_options |= K_USER;
	z_thread_essential_clear();
#ifdef CONFIG_THREAD_MONITOR
	_current->entry.pEntry = entry;
	_current->entry.parameter1 = p1;
	_current->entry.parameter2 = p2;
	_current->entry.parameter3 = p3;
#endif
#ifdef CONFIG_USERSPACE
	__ASSERT(z_stack_is_user_capable(_current->stack_obj),
		 "dropping to user mode with kernel-only stack object");
#ifdef CONFIG_THREAD_USERSPACE_LOCAL_DATA
	memset(_current->userspace_local_data, 0,
	       sizeof(struct _thread_userspace_local_data));
#endif
#ifdef CONFIG_THREAD_LOCAL_STORAGE
	arch_tls_stack_setup(_current,
			     (char *)(_current->stack_info.start +
				      _current->stack_info.size));
#endif
	arch_user_mode_enter(entry, p1, p2, p3);
#else
	/* XXX In this case we do not reset the stack */
	z_thread_entry(entry, p1, p2, p3);
#endif
}

/* These spinlock assertion predicates are defined here because having
 * them in spinlock.h is a giant header ordering headache.
 */
#ifdef CONFIG_SPIN_VALIDATE
bool z_spin_lock_valid(struct k_spinlock *l)
{
	uintptr_t thread_cpu = l->thread_cpu;

	if (thread_cpu != 0U) {
		if ((thread_cpu & 3U) == _current_cpu->id) {
			return false;
		}
	}
	return true;
}

bool z_spin_unlock_valid(struct k_spinlock *l)
{
	if (l->thread_cpu != (_current_cpu->id | (uintptr_t)_current)) {
		return false;
	}
	l->thread_cpu = 0;
	return true;
}

void z_spin_lock_set_owner(struct k_spinlock *l)
{
	l->thread_cpu = _current_cpu->id | (uintptr_t)_current;
}

#ifdef CONFIG_KERNEL_COHERENCE
bool z_spin_lock_mem_coherent(struct k_spinlock *l)
{
	return arch_mem_coherent((void *)l);
}
#endif /* CONFIG_KERNEL_COHERENCE */

#endif /* CONFIG_SPIN_VALIDATE */

int z_impl_k_float_disable(struct k_thread *thread)
{
#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
	return arch_float_disable(thread);
#else
	return -ENOTSUP;
#endif /* CONFIG_FPU && CONFIG_FPU_SHARING */
}

int z_impl_k_float_enable(struct k_thread *thread, unsigned int options)
{
#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
	return arch_float_enable(thread, options);
#else
	return -ENOTSUP;
#endif /* CONFIG_FPU && CONFIG_FPU_SHARING */
}

#ifdef CONFIG_USERSPACE
static inline int z_vrfy_k_float_disable(struct k_thread *thread)
{
	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));
	return z_impl_k_float_disable(thread);
}
#include <syscalls/k_float_disable_mrsh.c>
#endif /* CONFIG_USERSPACE */

#ifdef CONFIG_IRQ_OFFLOAD
/* Make offload_sem visible outside under testing, in order to release
 * it outside when error happened.
 */
K_SEM_DEFINE(offload_sem, 1, 1);

void irq_offload(irq_offload_routine_t routine, const void *parameter)
{
	k_sem_take(&offload_sem, K_FOREVER);
	arch_irq_offload(routine, parameter);
	k_sem_give(&offload_sem);
}
#endif

#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
#ifdef CONFIG_STACK_GROWS_UP
#error "Unsupported configuration for stack analysis"
#endif

int z_impl_k_thread_stack_space_get(const struct k_thread *thread,
				    size_t *unused_ptr)
{
	const uint8_t *start = (uint8_t *)thread->stack_info.start;
	size_t size = thread->stack_info.size;
	size_t unused = 0;
	const uint8_t *checked_stack = start;
	/* Take the address of any local variable as a shallow bound for the
	 * stack pointer.  Addresses above it are guaranteed to be
	 * accessible.
	 */
	const uint8_t *stack_pointer = (const uint8_t *)&start;

	/* If we are currently running on the stack being analyzed, some
	 * memory management hardware will generate an exception if we
	 * read unused stack memory.
	 *
	 * This never happens when invoked from user mode, as user mode
	 * will always run this function on the privilege elevation stack.
	 */
	if ((stack_pointer > start) && (stack_pointer <= (start + size)) &&
	    IS_ENABLED(CONFIG_NO_UNUSED_STACK_INSPECTION)) {
		/* TODO: We could add an arch_ API call to temporarily
		 * disable the stack checking in the CPU, but this would
		 * need to be properly managed wrt context switches/interrupts
		 */
		return -ENOTSUP;
	}

	if (IS_ENABLED(CONFIG_STACK_SENTINEL)) {
		/* First 4 bytes of the stack buffer reserved for the
		 * sentinel value, it won't be 0xAAAAAAAA for thread
		 * stacks.
		 *
		 * FIXME: thread->stack_info.start ought to reflect
		 * this!
		 */
		checked_stack += 4;
		size -= 4;
	}

	for (size_t i = 0; i < size; i++) {
		if ((checked_stack[i]) == 0xaaU) {
			unused++;
		} else {
			break;
		}
	}

	*unused_ptr = unused;

	return 0;
}

#ifdef CONFIG_USERSPACE
int z_vrfy_k_thread_stack_space_get(const struct k_thread *thread,
				    size_t *unused_ptr)
{
	size_t unused;
	int ret;

	ret = Z_SYSCALL_OBJ(thread, K_OBJ_THREAD);
	CHECKIF(ret != 0) {
		return ret;
	}

	ret = z_impl_k_thread_stack_space_get(thread, &unused);
	CHECKIF(ret != 0) {
		return ret;
	}

	ret = z_user_to_copy(unused_ptr, &unused, sizeof(size_t));
	CHECKIF(ret != 0) {
		return ret;
	}

	return 0;
}
#include <syscalls/k_thread_stack_space_get_mrsh.c>
#endif /* CONFIG_USERSPACE */
#endif /* CONFIG_INIT_STACKS && CONFIG_THREAD_STACK_INFO */

#ifdef CONFIG_USERSPACE
static inline k_ticks_t z_vrfy_k_thread_timeout_remaining_ticks(
						    const struct k_thread *t)
{
	Z_OOPS(Z_SYSCALL_OBJ(t, K_OBJ_THREAD));
	return z_impl_k_thread_timeout_remaining_ticks(t);
}
#include <syscalls/k_thread_timeout_remaining_ticks_mrsh.c>

static inline k_ticks_t z_vrfy_k_thread_timeout_expires_ticks(
						  const struct k_thread *t)
{
	Z_OOPS(Z_SYSCALL_OBJ(t, K_OBJ_THREAD));
	return z_impl_k_thread_timeout_expires_ticks(t);
}
#include <syscalls/k_thread_timeout_expires_ticks_mrsh.c>
#endif

#ifdef CONFIG_INSTRUMENT_THREAD_SWITCHING
void z_thread_mark_switched_in(void)
{
#ifdef CONFIG_TRACING
	SYS_PORT_TRACING_FUNC(k_thread, switched_in);
#endif

#ifdef CONFIG_THREAD_RUNTIME_STATS
	struct k_thread *thread;

	thread = z_current_get();
#ifdef CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
	thread->rt_stats.last_switched_in = timing_counter_get();
#else
	thread->rt_stats.last_switched_in = k_cycle_get_32();
#endif /* CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS */

#endif /* CONFIG_THREAD_RUNTIME_STATS */
}

void z_thread_mark_switched_out(void)
{
#ifdef CONFIG_THREAD_RUNTIME_STATS
#ifdef CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
	timing_t now;
#else
	uint32_t now;
#endif /* CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS */

	uint64_t diff;
	struct k_thread *thread;

	thread = z_current_get();

	if (unlikely(thread->rt_stats.last_switched_in == 0)) {
		/* Has not run before */
		return;
	}

	if (unlikely(thread->base.thread_state == _THREAD_DUMMY)) {
		/* dummy thread has no stat struct */
		return;
	}

#ifdef CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
	now = timing_counter_get();
	diff = timing_cycles_get(&thread->rt_stats.last_switched_in, &now);
#else
	now = k_cycle_get_32();
	diff = (uint64_t)(now - thread->rt_stats.last_switched_in);
	thread->rt_stats.last_switched_in = 0;
#endif /* CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS */

	thread->rt_stats.stats.execution_cycles += diff;

	threads_runtime_stats.execution_cycles += diff;
#endif /* CONFIG_THREAD_RUNTIME_STATS */

#ifdef CONFIG_TRACING
	SYS_PORT_TRACING_FUNC(k_thread, switched_out);
#endif
}

#ifdef CONFIG_THREAD_RUNTIME_STATS
int k_thread_runtime_stats_get(k_tid_t thread,
			       k_thread_runtime_stats_t *stats)
{
	if ((thread == NULL) || (stats == NULL)) {
		return -EINVAL;
	}

	(void)memcpy(stats, &thread->rt_stats.stats,
		     sizeof(thread->rt_stats.stats));

	return 0;
}

int k_thread_runtime_stats_all_get(k_thread_runtime_stats_t *stats)
{
	if (stats == NULL) {
		return -EINVAL;
	}

	(void)memcpy(stats, &threads_runtime_stats,
		     sizeof(threads_runtime_stats));

	return 0;
}
#endif /* CONFIG_THREAD_RUNTIME_STATS */

#endif /* CONFIG_INSTRUMENT_THREAD_SWITCHING */
