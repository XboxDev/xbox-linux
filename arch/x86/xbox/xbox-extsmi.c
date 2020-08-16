/**
 * Driver that handles the EXTSMI# interrupt on the xbox.
 * Makes it possible to use the eject-button without the xbox rebooting...
 *
 * smbus-command sequence to prevent reboot from SMC.
 *
 * Changelog:
 *  2003-01-14 Anders Gustafsson <andersg@0x63.nu>
 *             initial version
 *  2003-02-08 Milosch Meriac <xboxlinux@meriac.de>
 *             rewrote debug macros because of compiler errors
 *  2003-08-06 Michael Steil <mist@c64.org>
 *             removed Linux I2C dependency, now compiles
 *             without I2C in the kernel
 *
 * Todo: add errorhandling!
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <asm/io.h>
#include <linux/xbox.h>

#define IRQ 12

#define DRIVER_NAME "xbox-extsmi"
#define DRIVER_DESC "Original Xbox EXTSMI# eject IRQ handler"

/* Internal variable for ide-cd.c that specifies whether we simulate drive
   locking in software */
volatile int Xbox_simulate_drive_locked = 0;

#define BASE 0x8000

/* Power Management 1 Enable Register */
#define PM02 (BASE+0x02)

/* Power Management 1 Control Register */
#define PM04 (BASE+0x04)

/* ACPI GP Status Register */
#define PM20 (BASE+0x20)

/* ACPI GP Enable Register */
#define PM22 (BASE+0x22)
# define EXTSMI_EN_MASK 0x0002

/* Global SMI Enable Register */
#define PM2A (BASE+0x2A)

static DEFINE_MUTEX(extsmi_sem);
static struct task_struct *extsmi_task = NULL;

static irqreturn_t extsmi_interrupt(int irq, void *dev_id) {
	int reason;

	reason = inw(0x8020);
	outw(reason, 0x8020); /* ack  IS THIS NEEDED? */
	if (reason & 0x2) {
		/* wake up thread */
		mutex_unlock(&extsmi_sem);
	}

	return IRQ_HANDLED;
}

/**
 * Process an event. This is run in process-context.
 */
static void extsmi_process(void) {
	int reason = xbox_smc_read(SMC_CMD_INTERRUPT_REASON);

	if (reason & TRAYBUTTON_MASK) { /* Tray button! Respond to prevent reboot! */
		xbox_smc_write(SMC_CMD_INTERRUPT_RESPOND, SMC_SUBCMD_RESPOND_CONTINUE);
		xbox_smc_write(0x00, 0x0c);
		/* eject unless lock simulation is being used */
		if (!Xbox_simulate_drive_locked)
			xbox_tray_eject();
	}
}

static int extsmi_thread(void *data) {
	do {
		extsmi_process();
		mutex_lock_interruptible(&extsmi_sem);
	} while (!kthread_should_stop());

	extsmi_task = NULL;
	return 0;
}

static int __init extsmi_init(void) {
	int ret = -1;

	if (!machine_is_xbox())
		return ret;

	extsmi_task = kthread_run(extsmi_thread, NULL, DRIVER_NAME);
	if (IS_ERR(extsmi_task)) {
		extsmi_task = NULL;
		return ret;
	}

	printk(KERN_INFO DRIVER_NAME ": " DRIVER_DESC "\n");

	/* this shuts a lot of interrupts off! */
	outw(inw(0x80e2) & 0xf8c7, 0x80e2);
	outw(0, 0x80ac);
	outb(0, 0x8025);
	outw(EXTSMI_EN_MASK, PM22); /* enable the EXTSMI# interrupt! */
	outw(0, PM02);
	outb(1, PM04); /* enable sci interrupts! */
	xbox_smc_write(SMC_CMD_RESET_ON_EJECT, SMC_SUBCMD_RESET_ON_EJECT_DISABLE);

	if ((ret = request_irq(IRQ, extsmi_interrupt, IRQF_SHARED, DRIVER_NAME, (void *)1)))
		printk(KERN_ERR DRIVER_NAME ": failed to register eject IRQ handler. (%i)\n", ret);

	if (ret) {
		kthread_stop(extsmi_task);
		extsmi_task = NULL;
	}

	return ret;
}

static void __exit extsmi_exit(void) {
	if (!extsmi_task)
		return;

	free_irq(IRQ, (void *)1);
	kthread_stop(extsmi_task);
	extsmi_task = NULL;
}

module_init(extsmi_init);
module_exit(extsmi_exit);

MODULE_AUTHOR("Anders Gustafsson <andersg@0x63.nu>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
