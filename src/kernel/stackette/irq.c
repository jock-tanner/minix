/* The interrupt controller of the Stackette: eight lines, a mask register.
 * The structure is that of Minix's i8259.c. A line that is being handled is
 * closed in `kernel_mask` (and in the mask register when a user context runs,
 * see mpx.c); enable_irq() opens it again.
 */

#include "../kernel.h"
#include "../proc.h"
#include "stackette.h"

#define INTC_MASK_REG	PERIPH(0xf00a)

PUBLIC void intr_init(mine)
int mine;
{
  kernel_mask = 0;
  INTC_MASK_REG = 0;
}

PUBLIC void put_irq_handler(hook, irq, handler)
irq_hook_t *hook;
int irq;
irq_handler_t handler;
{
/* Register an interrupt handler. */
  int id;
  irq_hook_t **line;

  if ((unsigned) irq >= NR_IRQ_VECTORS)
	panic("invalid call to put_irq_handler", irq);

  line = &irq_hooks[irq];
  id = 1;
  while (*line != NULL) {
	if (hook == *line) return;	/* extra initialization */
	line = &(*line)->next;
	id <<= 1;
  }
  if (id == 0) panic("Too many handlers for irq", irq);

  hook->next = NULL;
  hook->handler = handler;
  hook->irq = irq;
  hook->id = id;
  *line = hook;

  irq_use |= 1 << irq;
}

PUBLIC void intr_handle(hook)
irq_hook_t *hook;
{
/* Call the interrupt handlers for an interrupt with the given hook list. The
 * line is closed already (mpx.c).
 */
  while (hook != NULL) {
	/* For each handler in the list, mark it active by setting its ID bit,
	 * call the function, and unmark it if the function returns true.
	 */
	irq_actids[hook->irq] |= hook->id;
	if ((*hook->handler)(hook)) irq_actids[hook->irq] &= ~hook->id;
	hook = hook->next;
  }
}

PUBLIC void enable_irq(hook)
irq_hook_t *hook;
{
/* Open the line again if no handler is still busy with it. */
  lock();
  if ((irq_actids[hook->irq] &= ~hook->id) == 0)
	kernel_mask |= 1 << hook->irq;
  unlock();
}

PUBLIC int disable_irq(hook)
irq_hook_t *hook;
{
/* Close the line; false if it was closed already. */
  int r = FALSE;

  lock();
  if (irq_use & (1 << hook->irq)) {
	if (irq_actids[hook->irq] == 0 && (kernel_mask & (1 << hook->irq)))
		r = TRUE;
	irq_actids[hook->irq] |= hook->id;
	kernel_mask &= ~(1 << hook->irq);
  }
  unlock();
  return(r);
}
