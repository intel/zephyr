/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_

#include <logging/log_msg.h>
#include <logging/log_core2.h>
#include <logging/log_instance.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <syscall.h>
#include <sys/util.h>

#define LOG_LEVEL_NONE 0U
#define LOG_LEVEL_ERR  1U
#define LOG_LEVEL_WRN  2U
#define LOG_LEVEL_INF  3U
#define LOG_LEVEL_DBG  4U

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_LOG
#define CONFIG_LOG_DEFAULT_LEVEL 0U
#define CONFIG_LOG_MAX_LEVEL 0U
#endif

#if !defined(CONFIG_LOG) || defined(CONFIG_LOG_MINIMAL)
#define CONFIG_LOG_DOMAIN_ID 0U
#endif

#define LOG_FUNCTION_PREFIX_MASK \
	(((uint32_t)IS_ENABLED(CONFIG_LOG_FUNC_NAME_PREFIX_ERR) << \
	  LOG_LEVEL_ERR) | \
	 ((uint32_t)IS_ENABLED(CONFIG_LOG_FUNC_NAME_PREFIX_WRN) << \
	  LOG_LEVEL_WRN) | \
	 ((uint32_t)IS_ENABLED(CONFIG_LOG_FUNC_NAME_PREFIX_INF) << \
	  LOG_LEVEL_INF) | \
	 ((uint32_t)IS_ENABLED(CONFIG_LOG_FUNC_NAME_PREFIX_DBG) << LOG_LEVEL_DBG))

/** @brief Macro for returning local level value if defined or default.
 *
 * Check @ref IS_ENABLED macro for detailed explanation of the trick.
 */
#define Z_LOG_RESOLVED_LEVEL(_level, _default) \
	Z_LOG_RESOLVED_LEVEL1(_level, _default)

#define Z_LOG_RESOLVED_LEVEL1(_level, _default) \
	__COND_CODE(_LOG_XXXX##_level, (_level), (_default))

#define _LOG_XXXX0  _LOG_YYYY,
#define _LOG_XXXX0U _LOG_YYYY,
#define _LOG_XXXX1  _LOG_YYYY,
#define _LOG_XXXX1U _LOG_YYYY,
#define _LOG_XXXX2  _LOG_YYYY,
#define _LOG_XXXX2U _LOG_YYYY,
#define _LOG_XXXX3  _LOG_YYYY,
#define _LOG_XXXX3U _LOG_YYYY,
#define _LOG_XXXX4  _LOG_YYYY,
#define _LOG_XXXX4U _LOG_YYYY,

/**
 * @brief Macro for conditional code generation if provided log level allows.
 *
 * Macro behaves similarly to standard \#if \#else \#endif clause. The
 * difference is that it is evaluated when used and not when header file is
 * included.
 *
 * @param _eval_level Evaluated level. If level evaluates to one of existing log
 *		      log level (1-4) then macro evaluates to _iftrue.
 * @param _iftrue     Code that should be inserted when evaluated to true. Note,
 *		      that parameter must be provided in brackets.
 * @param _iffalse    Code that should be inserted when evaluated to false.
 *		      Note, that parameter must be provided in brackets.
 */
#define Z_LOG_EVAL(_eval_level, _iftrue, _iffalse) \
	Z_LOG_EVAL1(_eval_level, _iftrue, _iffalse)

#define Z_LOG_EVAL1(_eval_level, _iftrue, _iffalse) \
	__COND_CODE(_LOG_ZZZZ##_eval_level, _iftrue, _iffalse)

#define _LOG_ZZZZ1  _LOG_YYYY,
#define _LOG_ZZZZ1U _LOG_YYYY,
#define _LOG_ZZZZ2  _LOG_YYYY,
#define _LOG_ZZZZ2U _LOG_YYYY,
#define _LOG_ZZZZ3  _LOG_YYYY,
#define _LOG_ZZZZ3U _LOG_YYYY,
#define _LOG_ZZZZ4  _LOG_YYYY,
#define _LOG_ZZZZ4U _LOG_YYYY,

/** @brief Macro for getting log level for given module.
 *
 * It is evaluated to LOG_LEVEL if defined. Otherwise CONFIG_LOG_DEFAULT_LEVEL
 * is used.
 */
#define _LOG_LEVEL() Z_LOG_RESOLVED_LEVEL(LOG_LEVEL, CONFIG_LOG_DEFAULT_LEVEL)

/**
 *  @def LOG_CONST_ID_GET
 *  @brief Macro for getting ID of the element of the section.
 *
 *  @param _addr Address of the element.
 */
#define LOG_CONST_ID_GET(_addr) \
	COND_CODE_1(CONFIG_LOG, ((__log_level ? log_const_source_id(_addr) : 0)), (0))

/**
 * @def LOG_CURRENT_MODULE_ID
 * @brief Macro for getting ID of current module.
 */
#define LOG_CURRENT_MODULE_ID() (__log_level != 0 ? \
	log_const_source_id(__log_current_const_data) : 0U)

/**
 * @def LOG_CURRENT_DYNAMIC_DATA_ADDR
 * @brief Macro for getting address of dynamic structure of current module.
 */
#define LOG_CURRENT_DYNAMIC_DATA_ADDR()	(__log_level ? \
	__log_current_dynamic_data : (struct log_source_dynamic_data *)0U)

/** @brief Macro for getting ID of the element of the section.
 *
 *  @param _addr Address of the element.
 */
#define LOG_DYNAMIC_ID_GET(_addr) \
	COND_CODE_1(CONFIG_LOG, ((__log_level ? log_dynamic_source_id(_addr) : 0)), (0))

/* Set of defines that are set to 1 if function name prefix is enabled for given level. */
#define Z_LOG_FUNC_PREFIX_0U 0
#define Z_LOG_FUNC_PREFIX_1U COND_CODE_1(CONFIG_LOG_FUNC_NAME_PREFIX_ERR, (1), (0))
#define Z_LOG_FUNC_PREFIX_2U COND_CODE_1(CONFIG_LOG_FUNC_NAME_PREFIX_WRN, (1), (0))
#define Z_LOG_FUNC_PREFIX_3U COND_CODE_1(CONFIG_LOG_FUNC_NAME_PREFIX_INF, (1), (0))
#define Z_LOG_FUNC_PREFIX_4U COND_CODE_1(CONFIG_LOG_FUNC_NAME_PREFIX_DBG, (1), (0))

/**
 * @brief Macro for optional injection of function name as first argument of
 *	  formatted string. COND_CODE_0() macro is used to handle no arguments
 *	  case.
 *
 * The purpose of this macro is to prefix string literal with format specifier
 * for function name and inject function name as first argument. In order to
 * handle string with no arguments _LOG_Z_EVAL is used.
 */
#define Z_LOG_STR_WITH_PREFIX2(...) \
	"%s: " GET_ARG_N(1, __VA_ARGS__), (const char *)__func__\
		COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__),\
			    (),\
			    (, GET_ARGS_LESS_N(1, __VA_ARGS__))\
			   )

/* Macro handles case when no format string is provided: e.g. LOG_DBG().
 * Handling of format string is deferred to the next level macro.
 */
#define Z_LOG_STR_WITH_PREFIX(...) \
	COND_CODE_0(NUM_VA_ARGS_LESS_1(_, ##__VA_ARGS__), \
		("%s", (const char *)__func__), \
		(Z_LOG_STR_WITH_PREFIX2(__VA_ARGS__)))

/**
 * @brief Handle optional injection of function name as the first argument.
 *
 * Additionally, macro is handling the empty message case.
 */
#define Z_LOG_STR(_level, ...) \
	COND_CODE_1(UTIL_CAT(Z_LOG_FUNC_PREFIX_##_level), \
		(Z_LOG_STR_WITH_PREFIX(__VA_ARGS__)), (__VA_ARGS__))

/******************************************************************************/
/****************** Internal macros for log frontend **************************/
/******************************************************************************/
/**@brief Second stage for Z_LOG_NARGS_POSTFIX */
#define Z_LOG_NARGS_POSTFIX_IMPL(				\
	_ignored,						\
	_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,		\
	_11, _12, _13, _14, N, ...) N

/**@brief Macro to get the postfix for further log message processing.
 *
 * Logs with more than 3 arguments are processed in a generic way.
 *
 * param[in]    ...     List of arguments
 *
 * @retval  Postfix, number of arguments or _LONG when more than 3 arguments.
 */
#define Z_LOG_NARGS_POSTFIX(...) \
	Z_LOG_NARGS_POSTFIX_IMPL(__VA_ARGS__, LONG, LONG, LONG, LONG, LONG, \
			LONG, LONG, LONG, LONG, LONG, LONG, LONG, 3, 2, 1, 0, ~)

#define Z_LOG_INTERNAL_X(N, ...)  UTIL_CAT(_LOG_INTERNAL_, N)(__VA_ARGS__)

#define Z_LOG_INTERNAL2(is_user_context, _src_level, ...) do { \
	if (is_user_context) { \
		log_from_user(_src_level, __VA_ARGS__); \
	} else if (IS_ENABLED(CONFIG_LOG_IMMEDIATE)) { \
		log_string_sync(_src_level, __VA_ARGS__); \
	} else { \
		Z_LOG_INTERNAL_X(Z_LOG_NARGS_POSTFIX(__VA_ARGS__), \
					_src_level, __VA_ARGS__); \
	} \
} while (false)

#define Z_LOG_INTERNAL(is_user_context, _level, _source, _dsource, ...) do { \
	uint16_t src_id = \
		IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? \
		LOG_DYNAMIC_ID_GET(_dsource) : LOG_CONST_ID_GET(_source); \
	struct log_msg_ids src_level = { \
		.level = _level, \
		.domain_id = CONFIG_LOG_DOMAIN_ID, \
		.source_id = src_id \
	}; \
	Z_LOG_INTERNAL2(is_user_context, src_level, \
			Z_LOG_STR(_level, __VA_ARGS__)); \
} while (0)

#define _LOG_INTERNAL_0(_src_level, _str) \
	log_0(_str, _src_level)

#define _LOG_INTERNAL_1(_src_level, _str, _arg0) \
	log_1(_str, (log_arg_t)(_arg0), _src_level)

#define _LOG_INTERNAL_2(_src_level, _str, _arg0, _arg1)	\
	log_2(_str, (log_arg_t)(_arg0), (log_arg_t)(_arg1), _src_level)

#define _LOG_INTERNAL_3(_src_level, _str, _arg0, _arg1, _arg2) \
	log_3(_str, (log_arg_t)(_arg0), (log_arg_t)(_arg1), (log_arg_t)(_arg2), _src_level)

#define __LOG_ARG_CAST(_x) (log_arg_t)(_x)

#define __LOG_ARGUMENTS(...) FOR_EACH(__LOG_ARG_CAST, (,), __VA_ARGS__)

#define _LOG_INTERNAL_LONG(_src_level, _str, ...)		  \
	do {							  \
		log_arg_t args[] = {__LOG_ARGUMENTS(__VA_ARGS__)};\
		log_n(_str, args, ARRAY_SIZE(args), _src_level);  \
	} while (false)

#define Z_LOG_LEVEL_CHECK(_level, _check_level, _default_level) \
	(_level <= Z_LOG_RESOLVED_LEVEL(_check_level, _default_level))

#define Z_LOG_CONST_LEVEL_CHECK(_level)					    \
	(IS_ENABLED(CONFIG_LOG) &&					    \
	(Z_LOG_LEVEL_CHECK(_level, CONFIG_LOG_OVERRIDE_LEVEL, LOG_LEVEL_NONE) \
	||								    \
	((IS_ENABLED(CONFIG_LOG_OVERRIDE_LEVEL) == false) &&		    \
	(_level <= __log_level) &&					    \
	(_level <= CONFIG_LOG_MAX_LEVEL)				    \
	)								    \
	))

/*****************************************************************************/
/****************** Defiinitions used by minimal logging *********************/
/*****************************************************************************/
void z_log_minimal_hexdump_print(int level, const void *data, size_t size);
void z_log_minimal_vprintk(const char *fmt, va_list ap);
void z_log_minimal_printk(const char *fmt, ...);

#define Z_LOG_TO_PRINTK(_level, fmt, ...) do { \
	z_log_minimal_printk("%c: " fmt "\n", \
			     z_log_minimal_level_to_char(_level), \
			     ##__VA_ARGS__); \
} while (false)

#define Z_LOG_TO_VPRINTK(_level, fmt, valist) do { \
	z_log_minimal_printk("%c: ", z_log_minimal_level_to_char(_level)); \
	z_log_minimal_vprintk(fmt, valist); \
	z_log_minimal_printk("\n"); \
} while (false)

static inline char z_log_minimal_level_to_char(int level)
{
	switch (level) {
	case LOG_LEVEL_ERR:
		return 'E';
	case LOG_LEVEL_WRN:
		return 'W';
	case LOG_LEVEL_INF:
		return 'I';
	case LOG_LEVEL_DBG:
		return 'D';
	default:
		return '?';
	}
}

#define Z_LOG_INST(_inst) COND_CODE_1(CONFIG_LOG, (_inst), NULL)

/*****************************************************************************/
/****************** Macros for standard logging ******************************/
/*****************************************************************************/
#define Z_LOG2(_level, _source, _dsource, ...) do { \
	if (!Z_LOG_CONST_LEVEL_CHECK(_level)) { \
		break; \
	} \
	if (IS_ENABLED(CONFIG_LOG_MINIMAL)) { \
		Z_LOG_TO_PRINTK(_level, __VA_ARGS__); \
		break; \
	} \
	\
	bool is_user_context = k_is_user_context(); \
	if (IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) && !is_user_context && \
	    _level > Z_LOG_RUNTIME_FILTER((_dsource)->filters)) { \
		break; \
	} \
	if (IS_ENABLED(CONFIG_LOG2)) { \
		int _mode; \
		void *_src = IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? \
			(void *)_dsource : (void *)_source; \
		Z_LOG_MSG2_CREATE(UTIL_NOT(IS_ENABLED(CONFIG_USERSPACE)), _mode, \
				  CONFIG_LOG_DOMAIN_ID, _src, _level, NULL,\
				  0, __VA_ARGS__); \
	} else { \
		Z_LOG_INTERNAL(is_user_context,	_level, \
				_source, _dsource, __VA_ARGS__);\
	} \
	if (false) { \
		/* Arguments checker present but never evaluated.*/ \
		/* Placed here to ensure that __VA_ARGS__ are*/ \
		/* evaluated once when log is enabled.*/ \
		z_log_printf_arg_checker(__VA_ARGS__); \
	} \
} while (false)

#define Z_LOG(_level, ...) \
	Z_LOG2(_level, __log_current_const_data, __log_current_dynamic_data, __VA_ARGS__)

#define Z_LOG_INSTANCE(_level, _inst, ...) \
	Z_LOG2(_level, \
		COND_CODE_1(CONFIG_LOG_RUNTIME_FILTERING, (NULL), (Z_LOG_INST(_inst))), \
		(struct log_source_dynamic_data *)COND_CODE_1( \
						CONFIG_LOG_RUNTIME_FILTERING, \
						(Z_LOG_INST(_inst)), (NULL)), \
		__VA_ARGS__)

/*****************************************************************************/
/****************** Macros for hexdump logging *******************************/
/*****************************************************************************/
#define Z_LOG_HEXDUMP2(_level, _source, _dsource, _data, _len, ...) do { \
	const char *_str = GET_ARG_N(1, __VA_ARGS__); \
	if (!Z_LOG_CONST_LEVEL_CHECK(_level)) {	\
		break; \
	} \
	bool is_user_context = k_is_user_context(); \
	\
	if (IS_ENABLED(CONFIG_LOG_MINIMAL)) { \
		Z_LOG_TO_PRINTK(_level, "%s", _str); \
		z_log_minimal_hexdump_print(_level, \
					    (const char *)_data, _len);\
		break; \
	} \
	if (IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) && !is_user_context && \
	    _level > Z_LOG_RUNTIME_FILTER((_dsource)->filters)) { \
		break; \
	} \
	if (IS_ENABLED(CONFIG_LOG2)) { \
		int mode; \
		void *_src = IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? \
			(void *)_dsource : (void *)_source; \
		Z_LOG_MSG2_CREATE(UTIL_NOT(IS_ENABLED(CONFIG_USERSPACE)), mode, \
				  CONFIG_LOG_DOMAIN_ID, _src, _level, \
				  _data, _len, \
				COND_CODE_0(NUM_VA_ARGS_LESS_1(_, ##__VA_ARGS__), \
					(), \
				  (COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__), \
					  ("%s", __VA_ARGS__), (__VA_ARGS__)))));\
		break; \
	} \
	uint16_t src_id = \
		IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? \
		LOG_DYNAMIC_ID_GET(_dsource) : LOG_CONST_ID_GET(_source);\
	struct log_msg_ids src_level = { \
		.level = _level, \
		.domain_id = CONFIG_LOG_DOMAIN_ID, \
		.source_id = src_id, \
	}; \
	if (is_user_context) { \
		log_hexdump_from_user(src_level, _str, \
				      (const char *)_data, _len); \
	} else if (IS_ENABLED(CONFIG_LOG_IMMEDIATE)) { \
		log_hexdump_sync(src_level, _str, (const char *)_data, _len); \
	} else { \
		log_hexdump(_str, (const char *)_data, _len, src_level); \
	} \
} while (false)

#define Z_LOG_HEXDUMP(_level, _data, _length, ...) \
	Z_LOG_HEXDUMP2(_level, \
		      __log_current_const_data, \
		      __log_current_dynamic_data, \
		      _data, _length, __VA_ARGS__)

#define Z_LOG_HEXDUMP_INSTANCE(_level, _inst, _data, _length, _str) \
	Z_LOG_HEXDUMP2(_level, \
		COND_CODE_1(CONFIG_LOG_RUNTIME_FILTERING, (NULL), (Z_LOG_INST(_inst))), \
		(struct log_source_dynamic_data *)COND_CODE_1( \
						CONFIG_LOG_RUNTIME_FILTERING, \
						(Z_LOG_INST(_inst)), (NULL)), \
		_data, _length, _str)

/*****************************************************************************/
/****************** Filtering macros *****************************************/
/*****************************************************************************/

/** @brief Number of bits used to encode log level. */
#define LOG_LEVEL_BITS 3U

/** @brief Filter slot size. */
#define LOG_FILTER_SLOT_SIZE LOG_LEVEL_BITS

/** @brief Number of slots in one word. */
#define LOG_FILTERS_NUM_OF_SLOTS (32 / LOG_FILTER_SLOT_SIZE)

/** @brief Slot mask. */
#define LOG_FILTER_SLOT_MASK (BIT(LOG_FILTER_SLOT_SIZE) - 1U)

/** @brief Bit offset of a slot.
 *
 *  @param _id Slot ID.
 */
#define LOG_FILTER_SLOT_SHIFT(_id) (LOG_FILTER_SLOT_SIZE * (_id))

#define LOG_FILTER_SLOT_GET(_filters, _id) \
	((*(_filters) >> LOG_FILTER_SLOT_SHIFT(_id)) & LOG_FILTER_SLOT_MASK)

#define LOG_FILTER_SLOT_SET(_filters, _id, _filter)		     \
	do {							     \
		*(_filters) &= ~(LOG_FILTER_SLOT_MASK <<	     \
				 LOG_FILTER_SLOT_SHIFT(_id));	     \
		*(_filters) |= ((_filter) & LOG_FILTER_SLOT_MASK) << \
			       LOG_FILTER_SLOT_SHIFT(_id);	     \
	} while (false)

#define LOG_FILTER_AGGR_SLOT_IDX 0

#define LOG_FILTER_AGGR_SLOT_GET(_filters) \
	LOG_FILTER_SLOT_GET(_filters, LOG_FILTER_AGGR_SLOT_IDX)

#define LOG_FILTER_FIRST_BACKEND_SLOT_IDX 1

/* Return aggregated (highest) level for all enabled backends, e.g. if there
 * are 3 active backends, one backend is set to get INF logs from a module and
 * two other backends are set for ERR, returned level is INF.
 */
#define Z_LOG_RUNTIME_FILTER(_filter) \
	LOG_FILTER_SLOT_GET(&_filter, LOG_FILTER_AGGR_SLOT_IDX)

/** @brief Log level value used to indicate log entry that should not be
 *	   formatted (raw string).
 */
#define LOG_LEVEL_INTERNAL_RAW_STRING LOG_LEVEL_NONE

extern struct log_source_const_data __log_const_start[];
extern struct log_source_const_data __log_const_end[];

/** @brief Enum with possible actions for strdup operation. */
enum log_strdup_action {
	LOG_STRDUP_SKIP,     /**< None RAM string duplication. */
	LOG_STRDUP_EXEC,     /**< Always duplicate RAM strings. */
	LOG_STRDUP_CHECK_EXEC/**< Duplicate RAM strings, if not dupl. before.*/
};

#define Z_LOG_PRINTK(...) do { \
	if (IS_ENABLED(CONFIG_LOG_MINIMAL) || !IS_ENABLED(CONFIG_LOG2)) { \
		z_log_minimal_printk(__VA_ARGS__); \
		break; \
	} \
	int _mode; \
	if (0) {\
		z_log_printf_arg_checker(__VA_ARGS__); \
	} \
	Z_LOG_MSG2_CREATE(!IS_ENABLED(CONFIG_USERSPACE), _mode, \
			  CONFIG_LOG_DOMAIN_ID, NULL, \
			  LOG_LEVEL_INTERNAL_RAW_STRING, NULL, 0, __VA_ARGS__);\
} while (0)


/** @brief Get name of the log source.
 *
 * @param source_id Source ID.
 * @return Name.
 */
static inline const char *log_name_get(uint32_t source_id)
{
	return __log_const_start[source_id].name;
}

/** @brief Get compiled level of the log source.
 *
 * @param source_id Source ID.
 * @return Level.
 */
static inline uint8_t log_compiled_level_get(uint32_t source_id)
{
	return __log_const_start[source_id].level;
}

/** @brief Get index of the log source based on the address of the constant data
 *         associated with the source.
 *
 * @param data Address of the constant data.
 *
 * @return Source ID.
 */
static inline uint32_t log_const_source_id(
				const struct log_source_const_data *data)
{
	return ((uint8_t *)data - (uint8_t *)__log_const_start)/
			sizeof(struct log_source_const_data);
}

/** @brief Get number of registered sources. */
static inline uint32_t log_sources_count(void)
{
	return log_const_source_id(__log_const_end);
}

extern struct log_source_dynamic_data __log_dynamic_start[];
extern struct log_source_dynamic_data __log_dynamic_end[];

/** @brief Creates name of variable and section for runtime log data.
 *
 *  @param _name Name.
 */
#define LOG_ITEM_DYNAMIC_DATA(_name) UTIL_CAT(log_dynamic_, _name)

#define LOG_INSTANCE_DYNAMIC_DATA(_module_name, _inst) \
	LOG_ITEM_DYNAMIC_DATA(LOG_INSTANCE_FULL_NAME(_module_name, _inst))

/** @brief Get pointer to the filter set of the log source.
 *
 * @param source_id Source ID.
 *
 * @return Pointer to the filter set.
 */
static inline uint32_t *log_dynamic_filters_get(uint32_t source_id)
{
	return &__log_dynamic_start[source_id].filters;
}

/** @brief Get index of the log source based on the address of the dynamic data
 *         associated with the source.
 *
 * @param data Address of the dynamic data.
 *
 * @return Source ID.
 */
static inline uint32_t log_dynamic_source_id(struct log_source_dynamic_data *data)
{
	return ((uint8_t *)data - (uint8_t *)__log_dynamic_start)/
			sizeof(struct log_source_dynamic_data);
}

/* Initialize runtime filters */
void z_log_runtime_filters_init(void);

/* Notify log_core that a backend was enabled. */
void z_log_notify_backend_enabled(void);

/** @brief Dummy function to trigger log messages arguments type checking. */
static inline __printf_like(1, 2)
void z_log_printf_arg_checker(const char *fmt, ...)
{
	ARG_UNUSED(fmt);
}

/** @brief Standard log with no arguments.
 *
 * @param str           String.
 * @param src_level	Log identification.
 */
void log_0(const char *str, struct log_msg_ids src_level);

/** @brief Standard log with one argument.
 *
 * @param str           String.
 * @param arg1	        First argument.
 * @param src_level	Log identification.
 */
void log_1(const char *str,
	   log_arg_t arg1,
	   struct log_msg_ids src_level);

/** @brief Standard log with two arguments.
 *
 * @param str           String.
 * @param arg1	        First argument.
 * @param arg2	        Second argument.
 * @param src_level	Log identification.
 */
void log_2(const char *str,
	   log_arg_t arg1,
	   log_arg_t arg2,
	   struct log_msg_ids src_level);

/** @brief Standard log with three arguments.
 *
 * @param str           String.
 * @param arg1	        First argument.
 * @param arg2	        Second argument.
 * @param arg3	        Third argument.
 * @param src_level	Log identification.
 */
void log_3(const char *str,
	   log_arg_t arg1,
	   log_arg_t arg2,
	   log_arg_t arg3,
	   struct log_msg_ids src_level);

/** @brief Standard log with arguments list.
 *
 * @param str		String.
 * @param args		Array with arguments.
 * @param narg		Number of arguments in the array.
 * @param src_level	Log identification.
 */
void log_n(const char *str,
	   log_arg_t *args,
	   uint32_t narg,
	   struct log_msg_ids src_level);

/** @brief Hexdump log.
 *
 * @param str		String.
 * @param data		Data.
 * @param length	Data length.
 * @param src_level	Log identification.
 */
void log_hexdump(const char *str, const void *data, uint32_t length,
		 struct log_msg_ids src_level);

/** @brief Process log message synchronously.
 *
 * @param src_level	Log message details.
 * @param fmt		String to format.
 * @param ...		Variable list of arguments.
 */
void log_string_sync(struct log_msg_ids src_level, const char *fmt, ...);

/** @brief Process log hexdump message synchronously.
 *
 * @param src_level	Log message details.
 * @param metadata	Raw string associated with the data.
 * @param data		Data.
 * @param len		Data length.
 */
void log_hexdump_sync(struct log_msg_ids src_level, const char *metadata,
		      const void *data, uint32_t len);

/**
 * @brief Writes a generic log message to the log.
 *
 * @note This function is intended to be used when porting other log systems.
 *
 * @param src_level      Log identification.
 * @param fmt            String to format.
 * @param ap             Poiner to arguments list.
 * @param strdup_action  Manages strdup activity.
 */
void log_generic(struct log_msg_ids src_level, const char *fmt, va_list ap,
		 enum log_strdup_action strdup_action);

/**
 * @brief Writes a generic log message to the logging v2.
 *
 * @note This function is intended to be used when porting other log systems.
 *
 * @param level          Log level..
 * @param fmt            String to format.
 * @param ap             Poiner to arguments list.
 */
static inline void log2_generic(uint8_t level, const char *fmt, va_list ap)
{
	z_log_msg2_runtime_vcreate(CONFIG_LOG_DOMAIN_ID, NULL, level,
				   NULL, 0, fmt, ap);
}

/**
 * @brief Returns number of arguments visible from format string.
 *
 * @note This function is intended to be used when porting other log systems.
 *
 * @param fmt     Format string.
 *
 * @return        Number of arguments.
 */
uint32_t log_count_args(const char *fmt);

/**
 * @brief Writes a generic log message to the log from user mode.
 *
 * @note This function is intended to be used internally
 *	 by the logging subsystem.
 */
void log_generic_from_user(struct log_msg_ids src_level,
			   const char *fmt, va_list ap);

/** @brief Check if address belongs to the memory pool used for transient.
 *
 * @param buf Buffer.
 *
 * @return True if address within the pool, false otherwise.
 */
bool log_is_strdup(const void *buf);

/** @brief Free allocated buffer.
 *
 * @param buf Buffer.
 */
void log_free(void *buf);

/**
 * @brief Get current number of allocated buffers for string duplicates.
 */
uint32_t log_get_strdup_pool_current_utilization(void);

/**
 * @brief Get maximal number of simultaneously allocated buffers for string
 *	  duplicates.
 *
 * Value can be used to determine pool size.
 */
uint32_t log_get_strdup_pool_utilization(void);

/**
 * @brief Get length of the longest string duplicated.
 *
 * Value can be used to determine buffer size in the string duplicates pool.
 */
uint32_t log_get_strdup_longest_string(void);

/** @brief Indicate to the log core that one log message has been dropped.
 *
 * @param buffered True if dropped message was already buffered and it is being
 * dropped to free space for another message. False if message is being dropped
 * because allocation failed.
 */
void z_log_dropped(bool buffered);

/** @brief Read and clear current drop indications counter.
 *
 * @return Dropped count.
 */
uint32_t z_log_dropped_read_and_clear(void);

/** @brief Check if there are any pending drop notifications.
 *
 * @retval true Pending unreported drop indications.
 * @retval false No pending unreported drop indications.
 */
bool z_log_dropped_pending(void);

/** @brief Log a message from user mode context.
 *
 * @note This function is intended to be used internally
 *	 by the logging subsystem.
 *
 * @param src_level    Log identification.
 * @param fmt          String to format.
 * @param ...          Variable list of arguments.
 */
void __printf_like(2, 3) log_from_user(struct log_msg_ids src_level,
				       const char *fmt, ...);

/* Internal function used by log_from_user(). */
__syscall void z_log_string_from_user(uint32_t src_level_val, const char *str);

/**
 * @brief Create mask with occurences of a string format specifiers (%s).
 *
 * Result is stored as the mask (argument n is n'th bit). Bit is set if string
 * format specifier was found.
 *
 * @param str String.
 * @param nargs Number of arguments in the string.
 *
 * @return Mask with %s format specifiers found.
 */
uint32_t z_log_get_s_mask(const char *str, uint32_t nargs);

/** @brief Log binary data (displayed as hexdump) from user mode context.
 *
 * @note This function is intended to be used internally
 *	 by the logging subsystem.
 *
 * @param src_level	Log identification.
 * @param metadata	Raw string associated with the data.
 * @param data		Data.
 * @param len		Data length.
 */
void log_hexdump_from_user(struct log_msg_ids src_level, const char *metadata,
			   const void *data, uint32_t len);

/* Internal function used by log_hexdump_from_user(). */
__syscall void z_log_hexdump_from_user(uint32_t src_level_val,
				       const char *metadata,
				       const uint8_t *data, uint32_t len);

/******************************************************************************/
/********** Mocros _VA operate on var-args parameters.          ***************/
/*********  Intended to be used when porting other log systems. ***************/
/*********  Shall be used in the log entry interface function.  ***************/
/*********  Speed optimized for up to three arguments number.   ***************/
/******************************************************************************/
#define Z_LOG_VA(_level, _str, _valist, _argnum, _strdup_action)\
	__LOG_VA(_level, \
		 __log_current_const_data, \
		 __log_current_dynamic_data, \
		 _str, _valist, _argnum, _strdup_action)

#define __LOG_VA(_level, _source, _dsource, _str, _valist, _argnum, _strdup_action) do { \
	if (!Z_LOG_CONST_LEVEL_CHECK(_level)) { \
		break; \
	} \
	if (IS_ENABLED(CONFIG_LOG_MINIMAL)) { \
		Z_LOG_TO_VPRINTK(_level, _str, _valist); \
		break; \
	} \
	\
	bool is_user_context = k_is_user_context(); \
	uint32_t filters = IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? \
						_dsource->filters : 0;\
	if (IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) && !is_user_context && \
	    _level > Z_LOG_RUNTIME_FILTER(filters)) { \
		break; \
	} \
	if (IS_ENABLED(CONFIG_LOG2)) { \
		z_log_msg2_runtime_vcreate(CONFIG_LOG_DOMAIN_ID, _source, \
					   _level, NULL, 0, _str, _valist); \
		break; \
	} \
	uint16_t _id = \
		IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? \
		LOG_DYNAMIC_ID_GET(_dsource) : LOG_CONST_ID_GET(_source);\
	struct log_msg_ids src_level = { \
		.level = _level, \
		.domain_id = CONFIG_LOG_DOMAIN_ID, \
		.source_id = _id \
	}; \
	__LOG_INTERNAL_VA(is_user_context, \
			src_level, \
			_str, _valist, _argnum, \
			_strdup_action); \
} while (false)

/**
 * @brief Inline function to perform strdup, used in __LOG_INTERNAL_VA macro
 *
 * @note This function is intended to be used when porting other log systems.
 *
 * @param msk	  Bitmask marking all %s arguments.
 * @param idx	  Index of actually processed argument.
 * @param param   Value of actually processed argument.
 * @param action  Action for strdup operation.
 *
 * @return	  Duplicated string or not changed param.
 */
static inline log_arg_t z_log_do_strdup(uint32_t msk, uint32_t idx,
					log_arg_t param,
					enum log_strdup_action action)
{
#ifndef CONFIG_LOG_MINIMAL
	char *z_log_strdup(const char *str);

	if (msk & (1 << idx)) {
		const char *str = (const char *)param;
		/* is_rodata(str) is not checked,
		 * because log_strdup does it.
		 * Hence, we will do only optional check
		 * if already not duplicated.
		 */
		if (action == LOG_STRDUP_EXEC || !log_is_strdup(str)) {
			param = (log_arg_t)z_log_strdup(str);
		}
	}
#else
	ARG_UNUSED(msk);
	ARG_UNUSED(idx);
	ARG_UNUSED(action);
#endif
	return param;
}

#define __LOG_INTERNAL_VA(is_user_context, _src_level, _str, _valist,	       \
						_argnum, _strdup_action)       \
do {									       \
	if (is_user_context) {						       \
		log_generic_from_user(_src_level, _str, _valist);	       \
	} else if (IS_ENABLED(CONFIG_LOG_IMMEDIATE)) {			       \
		log_generic(_src_level, _str, _valist, _strdup_action);        \
	} else if (_argnum == 0) {					       \
		_LOG_INTERNAL_0(_src_level, _str);			       \
	} else {							       \
		uint32_t mask = (_strdup_action != LOG_STRDUP_SKIP) ?	       \
			z_log_get_s_mask(_str, _argnum)			       \
			: 0;						       \
									       \
		if (_argnum == 1) {					       \
			_LOG_INTERNAL_1(_src_level, _str,		       \
				z_log_do_strdup(mask, 0,		       \
				  va_arg(_valist, log_arg_t), _strdup_action));\
		} else if (_argnum == 2) {				       \
			_LOG_INTERNAL_2(_src_level, _str,		       \
				z_log_do_strdup(mask, 0,		       \
				  va_arg(_valist, log_arg_t), _strdup_action), \
				z_log_do_strdup(mask, 1,		       \
				  va_arg(_valist, log_arg_t), _strdup_action));\
		} else if (_argnum == 3) {				       \
			_LOG_INTERNAL_3(_src_level, _str,		       \
				z_log_do_strdup(mask, 0,		       \
				  va_arg(_valist, log_arg_t), _strdup_action), \
				z_log_do_strdup(mask, 1,		       \
				  va_arg(_valist, log_arg_t), _strdup_action), \
				z_log_do_strdup(mask, 2,		       \
				  va_arg(_valist, log_arg_t), _strdup_action));\
		} else {						       \
			log_generic(_src_level, _str, _valist, _strdup_action);\
		}							       \
	}								       \
} while (false)

#include <syscalls/log_core.h>

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_ */
