#include <linux/sandbox.h>
#include <linux/export.h>

static struct sandbox_algorithm_ops ops = {
  .enter_callback = NULL,
  .syscall_callback = NULL,
  .open_callback = NULL,
  .connect_callback = NULL,
  .bind_callback = NULL,
	.kill_callback = NULL,
};

struct sandbox_algorithm_ops * sandbox_algorithm = &ops;

/* exported symbols */
EXPORT_SYMBOL(sandbox_algorithm);
