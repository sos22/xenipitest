/* Simple test of Xen's IPI infrastructure */
#include <asm/config.h>
#include <asm/percpu.h>
#include <xen/tasklet.h>
#include <xen/shutdown.h>
#include <asm/irq.h>
#include <asm/apic.h>
#include <xen/time.h>
#include <xen/timer.h>
#include <asm/smp.h>

#define NR_TRIPS        100000
#define INITIAL_DISCARD 10000

static int done_initialisation;
static int test_cpu_x;
static int test_cpu_y;

static long nr_trips;
static unsigned long start_time;
static unsigned long finish_time;
static unsigned long send_ipi_time;
static void run_ipi_test_tasklet(unsigned long ignore);
static DECLARE_TASKLET(ipi_test_tasklet, run_ipi_test_tasklet, 0);

fastcall void ipi_test_interrupt(void);

static void __smp_ipi_test_interrupt(void)
{
    cpumask_t mask;
    if (smp_processor_id() == test_cpu_x) {
	if (nr_trips == INITIAL_DISCARD) {
	    start_time = NOW();
	    send_ipi_time = 0;
	}
	if (nr_trips == NR_TRIPS + INITIAL_DISCARD) {
	    finish_time = NOW();
	    tasklet_schedule(&ipi_test_tasklet);
	    return;
	}
	nr_trips++;
	mask = cpumask_of_cpu(test_cpu_y);
	send_ipi_time -= NOW();
	send_IPI_mask(&mask, IPI_TEST_VECTOR);
	send_ipi_time += NOW();
    } else {
	mask = cpumask_of_cpu(test_cpu_x);
	send_IPI_mask(&mask, IPI_TEST_VECTOR);
    }
}

fastcall void smp_ipi_test_interrupt(struct cpu_user_regs *regs)
{
    struct cpu_user_regs *old_regs = set_irq_regs(regs);

    ack_APIC_irq();

    __smp_ipi_test_interrupt();

    set_irq_regs(old_regs);
}

static void run_ipi_test_tasklet(unsigned long ignore)
{
    cpumask_t mask;

    BUG_ON(!local_irq_is_enabled());

    if (!done_initialisation) {
	printk("Running initialisation; x2 apic enabled %d\n", x2apic_enabled);
	set_intr_gate(IPI_TEST_VECTOR, ipi_test_interrupt);
	test_cpu_x = 0;
	test_cpu_y = 1;
	done_initialisation = 1;
    } else {
	unsigned long time_taken = finish_time - start_time;
	printk("CPUs %d -> %d took %ld nanoseconds to perform %ld round trips; RTT %ldns\n",
	       test_cpu_x, test_cpu_y,
	       time_taken, nr_trips - INITIAL_DISCARD,
	       time_taken / (nr_trips - INITIAL_DISCARD));
	printk("%d -> %d send IPI time %ld nanoseconds (%ld each)\n",
	       test_cpu_x, test_cpu_y,
	       send_ipi_time,
	       send_ipi_time / (nr_trips - INITIAL_DISCARD));
	nr_trips = 0;
	test_cpu_y = next_cpu(test_cpu_y, cpu_online_map);
	if (test_cpu_y == test_cpu_x)
	    test_cpu_y = next_cpu(test_cpu_y, cpu_online_map);
	if (test_cpu_y == NR_CPUS) {
	    test_cpu_x = next_cpu(test_cpu_x, cpu_online_map);
	    if (test_cpu_x == NR_CPUS) {
		printk("Finished test\n");
		machine_restart(0);
	    }
	    test_cpu_y = 0;
	}
    }

    BUG_ON(test_cpu_x == test_cpu_y);

    if (test_cpu_x == smp_processor_id()) {
	local_irq_disable();
	__smp_ipi_test_interrupt();
	local_irq_enable();
    } else {
	mask = cpumask_of_cpu(test_cpu_x);
	send_IPI_mask(&mask, IPI_TEST_VECTOR);
    }
}

static void run_ipi_test(void *ignore)
{
    printk("IPI test timer fired.\n");
    tasklet_schedule(&ipi_test_tasklet);
}

void install_ipi_test(void)
{
    static struct timer initial_delay_timer;

    /* We try to delay starting the test by a few seconds, just to let
     * everything calm down a bit. */
    printk("Install IPI test\n");

    init_timer(&initial_delay_timer,
	       run_ipi_test,
	       NULL,
	       0);

    set_timer(&initial_delay_timer, NOW() + SECONDS(5));
}
