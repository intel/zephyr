/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log_output.h>
#include <logging/log_ctrl.h>
#include <logging/log.h>
#include <sys/__assert.h>
#include <sys/cbprintf.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>

#define LOG_COLOR_CODE_DEFAULT "\x1B[0m"
#define LOG_COLOR_CODE_RED     "\x1B[1;31m"
#define LOG_COLOR_CODE_YELLOW  "\x1B[1;33m"

#define HEXDUMP_BYTES_IN_LINE 16

#define  DROPPED_COLOR_PREFIX \
	Z_LOG_EVAL(CONFIG_LOG_BACKEND_SHOW_COLOR, (LOG_COLOR_CODE_RED), ())

#define DROPPED_COLOR_POSTFIX \
	Z_LOG_EVAL(CONFIG_LOG_BACKEND_SHOW_COLOR, (LOG_COLOR_CODE_DEFAULT), ())

static const char *const severity[] = {
	NULL,
	"err",
	"wrn",
	"inf",
	"dbg"
};

static const char *const colors[] = {
	NULL,
	LOG_COLOR_CODE_RED,     /* err */
	LOG_COLOR_CODE_YELLOW,  /* warn */
	NULL,                   /* info */
	NULL                    /* dbg */
};

static uint32_t freq;
static uint32_t timestamp_div;

extern void log_output_msg_syst_process(const struct log_output *output,
				struct log_msg *msg, uint32_t flag);
extern void log_output_string_syst_process(const struct log_output *output,
				struct log_msg_ids src_level,
				const char *fmt, va_list ap, uint32_t flag);
extern void log_output_hexdump_syst_process(const struct log_output *output,
				struct log_msg_ids src_level,
				const uint8_t *data, uint32_t length, uint32_t flag);

/* The RFC 5424 allows very flexible mapping and suggest the value 0 being the
 * highest severity and 7 to be the lowest (debugging level) severity.
 *
 *    0   Emergency      System is unusable
 *    1   Alert          Action must be taken immediately
 *    2   Critical       Critical conditions
 *    3   Error          Error conditions
 *    4   Warning        Warning conditions
 *    5   Notice         Normal but significant condition
 *    6   Informational  Informational messages
 *    7   Debug          Debug-level messages
 */
static int level_to_rfc5424_severity(uint32_t level)
{
	uint8_t ret;

	switch (level) {
	case LOG_LEVEL_NONE:
		ret = 7U;
		break;
	case LOG_LEVEL_ERR:
		ret =  3U;
		break;
	case LOG_LEVEL_WRN:
		ret =  4U;
		break;
	case LOG_LEVEL_INF:
		ret =  6U;
		break;
	case LOG_LEVEL_DBG:
		ret = 7U;
		break;
	default:
		ret = 7U;
		break;
	}

	return ret;
}

static int out_func(int c, void *ctx)
{
	const struct log_output *out_ctx = (const struct log_output *)ctx;
	int idx;

	if (IS_ENABLED(CONFIG_LOG_IMMEDIATE)) {
		/* Backend must be thread safe in synchronous operation. */
		/* Need that step for big endian */
		char x = (char)c;

		out_ctx->func((uint8_t *)&x, 1, out_ctx->control_block->ctx);
		return 0;
	}

	if (out_ctx->control_block->offset == out_ctx->size) {
		log_output_flush(out_ctx);
	}

	idx = atomic_inc(&out_ctx->control_block->offset);
	out_ctx->buf[idx] = (uint8_t)c;

	__ASSERT_NO_MSG(out_ctx->control_block->offset <= out_ctx->size);

	return 0;
}

static int cr_out_func(int c, void *ctx)
{
	out_func(c, ctx);
	if (c == '\n') {
		out_func((int)'\r', ctx);
	}

	return 0;
}

static int print_formatted(const struct log_output *output,
			   const char *fmt, ...)
{
	va_list args;
	int length = 0;

	va_start(args, fmt);
	length = cbvprintf(out_func, (void *)output, fmt, args);
	va_end(args);

	return length;
}

static void buffer_write(log_output_func_t outf, uint8_t *buf, size_t len,
			 void *ctx)
{
	int processed;

	do {
		processed = outf(buf, len, ctx);
		len -= processed;
		buf += processed;
	} while (len != 0);
}


void log_output_flush(const struct log_output *output)
{
	buffer_write(output->func, output->buf,
		     output->control_block->offset,
		     output->control_block->ctx);

	output->control_block->offset = 0;
}

static int timestamp_print(const struct log_output *output,
			   uint32_t flags, uint32_t timestamp)
{
	int length;
	bool format =
		(flags & LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP) |
		(flags & LOG_OUTPUT_FLAG_FORMAT_SYSLOG);


	if (!format) {
		length = print_formatted(output, "[%08lu] ", timestamp);
	} else if (freq != 0U) {
		uint32_t total_seconds;
		uint32_t remainder;
		uint32_t seconds;
		uint32_t hours;
		uint32_t mins;
		uint32_t ms;
		uint32_t us;

		timestamp /= timestamp_div;
		total_seconds = timestamp / freq;
		seconds = total_seconds;
		hours = seconds / 3600U;
		seconds -= hours * 3600U;
		mins = seconds / 60U;
		seconds -= mins * 60U;

		remainder = timestamp % freq;
		ms = (remainder * 1000U) / freq;
		us = (1000 * (remainder * 1000U - (ms * freq))) / freq;

		if (IS_ENABLED(CONFIG_LOG_BACKEND_NET) &&
		    flags & LOG_OUTPUT_FLAG_FORMAT_SYSLOG) {
#if defined(CONFIG_NEWLIB_LIBC)
			char time_str[sizeof("1970-01-01T00:00:00")];
			struct tm *tm;
			time_t time;

			time = total_seconds;
			tm = gmtime(&time);

			strftime(time_str, sizeof(time_str), "%FT%T", tm);

			length = print_formatted(output, "%s.%06uZ ",
						 time_str, ms * 1000U + us);
#else
			length = print_formatted(output,
					"1970-01-01T%02u:%02u:%02u.%06uZ ",
					hours, mins, seconds, ms * 1000U + us);
#endif
		} else {
			length = print_formatted(output,
						 "[%02u:%02u:%02u.%03u,%03u] ",
						 hours, mins, seconds, ms, us);
		}
	} else {
		length = 0;
	}

	return length;
}

static void color_print(const struct log_output *output,
			bool color, bool start, uint32_t level)
{
	if (color) {
		const char *log_color = start && (colors[level] != NULL) ?
				colors[level] : LOG_COLOR_CODE_DEFAULT;
		print_formatted(output, "%s", log_color);
	}
}

static void color_prefix(const struct log_output *output,
			 bool color, uint32_t level)
{
	color_print(output, color, true, level);
}

static void color_postfix(const struct log_output *output,
			  bool color, uint32_t level)
{
	color_print(output, color, false, level);
}


static int ids_print(const struct log_output *output, bool level_on,
		     bool func_on, uint32_t domain_id, int16_t source_id,
		     uint32_t level)
{
	int total = 0;

	if (level_on) {
		total += print_formatted(output, "<%s> ", severity[level]);
	}

	if (source_id >= 0) {
		total += print_formatted(output,
				(func_on &&
				((1 << level) & LOG_FUNCTION_PREFIX_MASK)) ?
				"%s." : "%s: ",
				log_source_name_get(domain_id, source_id));
	}

	return total;
}

static void newline_print(const struct log_output *ctx, uint32_t flags)
{
	if (IS_ENABLED(CONFIG_LOG_BACKEND_NET) &&
	    flags & LOG_OUTPUT_FLAG_FORMAT_SYSLOG) {
		return;
	}

	if ((flags & LOG_OUTPUT_FLAG_CRLF_NONE) != 0U) {
		return;
	}

	if ((flags & LOG_OUTPUT_FLAG_CRLF_LFONLY) != 0U) {
		print_formatted(ctx, "\n");
	} else {
		print_formatted(ctx, "\r\n");
	}
}

static void std_print(struct log_msg *msg,
		      const struct log_output *output)
{
	const char *str = log_msg_str_get(msg);
	uint32_t nargs = log_msg_nargs_get(msg);
	log_arg_t *args = alloca(sizeof(log_arg_t)*nargs);
	int i;

	for (i = 0; i < nargs; i++) {
		args[i] = log_msg_arg_get(msg, i);
	}

	switch (log_msg_nargs_get(msg)) {
	case 0:
		print_formatted(output, str);
		break;
	case 1:
		print_formatted(output, str, args[0]);
		break;
	case 2:
		print_formatted(output, str, args[0], args[1]);
		break;
	case 3:
		print_formatted(output, str, args[0], args[1], args[2]);
		break;
	case 4:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3]);
		break;
	case 5:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4]);
		break;
	case 6:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5]);
		break;
	case 7:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6]);
		break;
	case 8:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6], args[7]);
		break;
	case 9:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6],  args[7],
				args[8]);
		break;
	case 10:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6],  args[7],
				args[8], args[9]);
		break;
	case 11:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6],  args[7],
				args[8], args[9], args[10]);
		break;
	case 12:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6],  args[7],
				args[8], args[9], args[10], args[11]);
		break;
	case 13:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6],  args[7],
				args[8], args[9], args[10], args[11], args[12]);
		break;
	case 14:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6],  args[7],
				args[8], args[9], args[10], args[11], args[12],
				args[13]);
		break;
	case 15:
		print_formatted(output, str, args[0], args[1], args[2],
				args[3], args[4], args[5], args[6],  args[7],
				args[8], args[9], args[10], args[11], args[12],
				args[13], args[14]);
		break;
	default:
		/* Unsupported number of arguments. */
		__ASSERT_NO_MSG(true);
		break;
	}
}

static void hexdump_line_print(const struct log_output *output,
			       const uint8_t *data, uint32_t length,
			       int prefix_offset, uint32_t flags)
{
	newline_print(output, flags);

	for (int i = 0; i < prefix_offset; i++) {
		print_formatted(output, " ");
	}

	for (int i = 0; i < HEXDUMP_BYTES_IN_LINE; i++) {
		if (i > 0 && !(i % 8)) {
			print_formatted(output, " ");
		}

		if (i < length) {
			print_formatted(output, "%02x ", data[i]);
		} else {
			print_formatted(output, "   ");
		}
	}

	print_formatted(output, "|");

	for (int i = 0; i < HEXDUMP_BYTES_IN_LINE; i++) {
		if (i > 0 && !(i % 8)) {
			print_formatted(output, " ");
		}

		if (i < length) {
			char c = (char)data[i];

			print_formatted(output, "%c",
			      isprint((int)c) ? c : '.');
		} else {
			print_formatted(output, " ");
		}
	}
}

static void hexdump_print(struct log_msg *msg,
			  const struct log_output *output,
			  int prefix_offset, uint32_t flags)
{
	uint32_t offset = 0U;
	uint8_t buf[HEXDUMP_BYTES_IN_LINE];
	size_t length;

	print_formatted(output, "%s", log_msg_str_get(msg));

	do {
		length = sizeof(buf);
		log_msg_hexdump_data_get(msg, buf, &length, offset);

		if (length) {
			hexdump_line_print(output, buf, length,
					   prefix_offset, flags);
			offset += length;
		} else {
			break;
		}
	} while (true);
}

static void log_msg2_hexdump(const struct log_output *output,
			     uint8_t *data, uint32_t len,
			     int prefix_offset, uint32_t flags)
{
	size_t length;

	do {
		length = MIN(len, HEXDUMP_BYTES_IN_LINE);

		hexdump_line_print(output, data, length,
				   prefix_offset, flags);
		data += length;
		len -= length;
	} while (len);
}


static void raw_string_print(struct log_msg *msg,
			     const struct log_output *output)
{
	__ASSERT_NO_MSG(output->size);

	size_t offset = 0;
	size_t length;
	bool eol = false;

	do {
		length = output->size;
		/* Sting is stored in a hexdump message. */
		log_msg_hexdump_data_get(msg, output->buf, &length, offset);
		output->control_block->offset = length;

		if (length != 0) {
			eol = (output->buf[length - 1] == '\n');
		}

		log_output_flush(output);
		offset += length;
	} while (length > 0);

	if (eol) {
		print_formatted(output, "\r");
	}
}

static uint32_t prefix_print(const struct log_output *output,
			 uint32_t flags, bool func_on, uint32_t timestamp, uint8_t level,
			 uint8_t domain_id, int16_t source_id)
{
	uint32_t length = 0U;

	bool stamp = flags & LOG_OUTPUT_FLAG_TIMESTAMP;
	bool colors_on = flags & LOG_OUTPUT_FLAG_COLORS;
	bool level_on = flags & LOG_OUTPUT_FLAG_LEVEL;

	if (IS_ENABLED(CONFIG_LOG_BACKEND_NET) &&
	    flags & LOG_OUTPUT_FLAG_FORMAT_SYSLOG) {
		/* TODO: As there is no way to figure out the
		 * facility at this point, use a pre-defined value.
		 * Change this to use the real facility of the
		 * logging call when that info is available.
		 */
		static const int facility = 16; /* local0 */

		length += print_formatted(
			output,
			"<%d>1 ",
			facility * 8 +
			level_to_rfc5424_severity(level));
	}

	if (stamp) {
		length += timestamp_print(output, flags, timestamp);
	}

	if (IS_ENABLED(CONFIG_LOG_BACKEND_NET) &&
	    flags & LOG_OUTPUT_FLAG_FORMAT_SYSLOG) {
		length += print_formatted(
			output, "%s - - - - ",
			output->control_block->hostname ?
			output->control_block->hostname :
			"zephyr");
	} else {
		color_prefix(output, colors_on, level);
	}

	length += ids_print(output, level_on, func_on,
			domain_id, source_id, level);


	return length;
}

static void postfix_print(const struct log_output *output,
			  uint32_t flags, uint8_t level)
{
	color_postfix(output, (flags & LOG_OUTPUT_FLAG_COLORS),
			      level);
	newline_print(output, flags);
}

void log_output_msg_process(const struct log_output *output,
			    struct log_msg *msg,
			    uint32_t flags)
{
	bool std_msg = log_msg_is_std(msg);
	uint32_t timestamp = log_msg_timestamp_get(msg);
	uint8_t level = (uint8_t)log_msg_level_get(msg);
	uint8_t domain_id = (uint8_t)log_msg_domain_id_get(msg);
	int16_t source_id = (int16_t)log_msg_source_id_get(msg);
	bool raw_string = (level == LOG_LEVEL_INTERNAL_RAW_STRING);
	int prefix_offset;

	if (IS_ENABLED(CONFIG_LOG_MIPI_SYST_ENABLE) &&
	    flags & LOG_OUTPUT_FLAG_FORMAT_SYST) {
		log_output_msg_syst_process(output, msg, flags);
		return;
	}

	prefix_offset = raw_string ?
			0 : prefix_print(output, flags, std_msg, timestamp,
					 level, domain_id, source_id);

	if (log_msg_is_std(msg)) {
		std_print(msg, output);
	} else if (raw_string) {
		raw_string_print(msg, output);
	} else {
		hexdump_print(msg, output, prefix_offset, flags);
	}

	if (!raw_string) {
		postfix_print(output, flags, level);
	}

	log_output_flush(output);
}

void log_output_msg2_process(const struct log_output *output,
			     struct log_msg2 *msg, uint32_t flags)
{
	log_timestamp_t timestamp = log_msg2_get_timestamp(msg);
	uint8_t level = log_msg2_get_level(msg);
	bool raw_string = (level == LOG_LEVEL_INTERNAL_RAW_STRING);
	uint32_t prefix_offset;

	if (IS_ENABLED(CONFIG_LOG_MIPI_SYST_ENABLE) &&
	    flags & LOG_OUTPUT_FLAG_FORMAT_SYST) {
		__ASSERT_NO_MSG(0);
		/* todo not supported
		 * log_output_msg_syst_process(output, msg, flags);
		 */
		return;
	}

	if (!raw_string) {
		void *source = (void *)log_msg2_get_source(msg);
		uint8_t domain_id = log_msg2_get_domain(msg);
		int16_t source_id = source ?
			(IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ?
				log_dynamic_source_id(source) :
				log_const_source_id(source)) :
			-1;

		prefix_offset = prefix_print(output, flags, 0, timestamp,
					 level, domain_id, source_id);
	} else {
		prefix_offset = 0;
	}

	size_t len;
	uint8_t *data = log_msg2_get_package(msg, &len);

	if (len) {
		int err = cbpprintf(raw_string ? cr_out_func :  out_func,
				    (void *)output, data);

		(void)err;
		__ASSERT_NO_MSG(err >= 0);
	}

	data = log_msg2_get_data(msg, &len);
	if (len) {
		log_msg2_hexdump(output, data, len, prefix_offset, flags);
	}

	if (!raw_string) {
		postfix_print(output, flags, level);
	}

	log_output_flush(output);
}

static bool ends_with_newline(const char *fmt)
{
	char c = '\0';

	while (*fmt != '\0') {
		c = *fmt;
		fmt++;
	}

	return (c == '\n');
}

void log_output_string(const struct log_output *output,
		       struct log_msg_ids src_level, uint32_t timestamp,
		       const char *fmt, va_list ap, uint32_t flags)
{
	int length;
	uint8_t level = (uint8_t)src_level.level;
	uint8_t domain_id = (uint8_t)src_level.domain_id;
	int16_t source_id = (int16_t)src_level.source_id;
	bool raw_string = (level == LOG_LEVEL_INTERNAL_RAW_STRING);

	if (IS_ENABLED(CONFIG_LOG_MIPI_SYST_ENABLE) &&
	    flags & LOG_OUTPUT_FLAG_FORMAT_SYST) {
		log_output_string_syst_process(output,
				src_level, fmt, ap, flags);
		return;
	}

	if (!raw_string) {
		prefix_print(output, flags, true, timestamp,
				level, domain_id, source_id);
	}

	length = cbvprintf(out_func, (void *)output, fmt, ap);

	(void)length;

	if (raw_string) {
		/* add \r if string ends with newline. */
		if (ends_with_newline(fmt)) {
			print_formatted(output, "\r");
		}
	} else {
		postfix_print(output, flags, level);
	}

	log_output_flush(output);
}

void log_output_hexdump(const struct log_output *output,
			     struct log_msg_ids src_level, uint32_t timestamp,
			     const char *metadata, const uint8_t *data,
			     uint32_t length, uint32_t flags)
{
	uint32_t prefix_offset;
	uint8_t level = (uint8_t)src_level.level;
	uint8_t domain_id = (uint8_t)src_level.domain_id;
	int16_t source_id = (int16_t)src_level.source_id;

	if (IS_ENABLED(CONFIG_LOG_MIPI_SYST_ENABLE) &&
	    flags & LOG_OUTPUT_FLAG_FORMAT_SYST) {
		log_output_hexdump_syst_process(output,
				src_level, data, length, flags);
		return;
	}

	prefix_offset = prefix_print(output, flags, true, timestamp,
				     level, domain_id, source_id);

	/* Print metadata */
	print_formatted(output, "%s", metadata);

	while (length != 0U) {
		uint32_t part_len = length > HEXDUMP_BYTES_IN_LINE ?
				HEXDUMP_BYTES_IN_LINE : length;

		hexdump_line_print(output, data, part_len,
				   prefix_offset, flags);

		data += part_len;
		length -= part_len;
	}

	postfix_print(output, flags, level);
	log_output_flush(output);
}

void log_output_dropped_process(const struct log_output *output, uint32_t cnt)
{
	char buf[5];
	int len;
	static const char prefix[] = DROPPED_COLOR_PREFIX "--- ";
	static const char postfix[] =
			" messages dropped ---\r\n" DROPPED_COLOR_POSTFIX;
	log_output_func_t outf = output->func;

	cnt = MIN(cnt, 9999);
	len = snprintk(buf, sizeof(buf), "%d", cnt);

	buffer_write(outf, (uint8_t *)prefix, sizeof(prefix) - 1,
		     output->control_block->ctx);
	buffer_write(outf, buf, len, output->control_block->ctx);
	buffer_write(outf, (uint8_t *)postfix, sizeof(postfix) - 1,
		     output->control_block->ctx);
}

void log_output_timestamp_freq_set(uint32_t frequency)
{
	timestamp_div = 1U;
	/* There is no point to have frequency higher than 1MHz (ns are not
	 * printed) and too high frequency leads to overflows in calculations.
	 */
	while (frequency > 1000000) {
		frequency /= 2U;
		timestamp_div *= 2U;
	}

	freq = frequency;
}

uint64_t log_output_timestamp_to_us(uint32_t timestamp)
{
	timestamp /= timestamp_div;

	return ((uint64_t) timestamp * 1000000U) / freq;
}
