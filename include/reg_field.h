#ifndef _REG_FIELD_H
#define _REG_FIELD_H

#include <stdint.h>
#include <stdarg.h>

enum cmd_op {
	CMD_OP_GET	= (1 << 0),
	CMD_OP_SET	= (1 << 1),
	CMD_OP_EXEC	= (1 << 2),
};

enum pstate {
	ST_IN_CMD,
	ST_IN_ARG,
};

struct strbuf {
	uint8_t idx;
	char buf[32];
};

struct cmd_state {
	struct strbuf cmd;
	struct strbuf arg;
	enum pstate state;
	void (*out)(const char *format, va_list ap);
};

struct cmd {
	const char *cmd;
	uint32_t ops;
	int (*cb)(struct cmd_state *cs, enum cmd_op op, const char *cmd,
		  int argc, char **argv);
	const char *help;
};

/* structure describing a field in a register */
struct reg_field {
	uint8_t reg;
	uint8_t shift;
	uint8_t width;
};

struct reg_field_ops {
	const struct reg_field *fields;
	const char **field_names;
	uint32_t num_fields;
	void *data;
	int (*write_cb)(void *data, uint32_t reg, uint32_t val);
	uint32_t (*read_cb)(void *data, uint32_t reg);
};

uint32_t reg_field_read(struct reg_field_ops *ops, struct reg_field *field);
int reg_field_write(struct reg_field_ops *ops, struct reg_field *field, uint32_t val);
int reg_field_cmd(struct cmd_state *cs, enum cmd_op op,
		  const char *cmd, int argc, char **argv,
		  struct reg_field_ops *ops);

#endif
