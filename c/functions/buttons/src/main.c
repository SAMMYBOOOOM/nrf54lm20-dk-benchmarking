/*
 * Four-button demo for Zephyr.
 *
 * Pressing button N (1..4):
 *   - prints "hello button N from con 11" on the console
 *   - blinks LED N exactly N times
 *
 * Devicetree requirements (aliases):
 *   sw0, sw1, sw2, sw3   -> the four buttons
 *   led0, led1, led2, led3 -> the four LEDs
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define NUM_BUTTONS     4

#define BLINK_ON_MS     200
#define BLINK_OFF_MS    200
#define DEBOUNCE_MS     50

/* ---- Devicetree aliases ---------------------------------------------- */
#define BTN0_NODE DT_ALIAS(sw0)
#define BTN1_NODE DT_ALIAS(sw1)
#define BTN2_NODE DT_ALIAS(sw2)
#define BTN3_NODE DT_ALIAS(sw3)

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)
#define LED3_NODE DT_ALIAS(led3)

#if !DT_NODE_HAS_STATUS(BTN0_NODE, okay) || \
    !DT_NODE_HAS_STATUS(BTN1_NODE, okay) || \
    !DT_NODE_HAS_STATUS(BTN2_NODE, okay) || \
    !DT_NODE_HAS_STATUS(BTN3_NODE, okay)
#error "This sample needs aliases sw0, sw1, sw2 and sw3 in devicetree."
#endif

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED1_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED2_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED3_NODE, okay)
#error "This sample needs aliases led0, led1, led2 and led3 in devicetree."
#endif

static const struct gpio_dt_spec buttons[NUM_BUTTONS] = {
	GPIO_DT_SPEC_GET(BTN0_NODE, gpios),
	GPIO_DT_SPEC_GET(BTN1_NODE, gpios),
	GPIO_DT_SPEC_GET(BTN2_NODE, gpios),
	GPIO_DT_SPEC_GET(BTN3_NODE, gpios),
};

static const struct gpio_dt_spec leds[NUM_BUTTONS] = {
	GPIO_DT_SPEC_GET(LED0_NODE, gpios),
	GPIO_DT_SPEC_GET(LED1_NODE, gpios),
	GPIO_DT_SPEC_GET(LED2_NODE, gpios),
	GPIO_DT_SPEC_GET(LED3_NODE, gpios),
};

/* ---- Per-button context ---------------------------------------------- */
struct btn_ctx {
	struct gpio_callback   cb;         /* button interrupt callback  */
	struct k_work_delayable blink;     /* blinking work item         */
	int blinks_left;                   /* ON phases still to show    */
	bool led_on;                       /* current LED state          */
	int64_t last_press_ms;             /* debounce timestamp         */
	int id;                            /* 1..4                       */
};

static struct btn_ctx ctx[NUM_BUTTONS];

/* ---- Blinking work handler ------------------------------------------- */
static void blink_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct btn_ctx *c = CONTAINER_OF(dwork, struct btn_ctx, blink);
	const struct gpio_dt_spec *led = &leds[c->id - 1];

	if (!c->led_on) {
		/* Start of an ON phase */
		gpio_pin_set_dt(led, 1);
		c->led_on = true;
		k_work_reschedule(dwork, K_MSEC(BLINK_ON_MS));
	} else {
		/* End of an ON phase */
		gpio_pin_set_dt(led, 0);
		c->led_on = false;
		c->blinks_left--;

		if (c->blinks_left > 0) {
			k_work_reschedule(dwork, K_MSEC(BLINK_OFF_MS));
		}
	}
}

/* ---- Button interrupt handler (ISR context) -------------------------- */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
			   uint32_t pins)
{
	struct btn_ctx *c = CONTAINER_OF(cb, struct btn_ctx, cb);
	int64_t now = k_uptime_get();

	/* Simple debounce */
	if ((now - c->last_press_ms) < DEBOUNCE_MS) {
		return;
	}
	c->last_press_ms = now;

	printk("hello button %d\n", c->id);

	/* Restart the blink sequence from scratch */
	c->blinks_left = c->id;
	c->led_on = false;
	gpio_pin_set_dt(&leds[c->id - 1], 0);

	k_work_reschedule(&c->blink, K_NO_WAIT);
}

/* ---- Initialisation --------------------------------------------------- */
int main(void)
{
	int ret;

	for (int i = 0; i < NUM_BUTTONS; i++) {
		if (!gpio_is_ready_dt(&leds[i])) {
			printk("LED %d port not ready\n", i);
			return 0;
		}
		if (!gpio_is_ready_dt(&buttons[i])) {
			printk("Button %d port not ready\n", i);
			return 0;
		}

		/* LED as output, initially off */
		ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			printk("Failed to configure LED %d (%d)\n", i, ret);
			return 0;
		}

		/* Button as input; flags (active low etc.) come from DT */
		ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
		if (ret < 0) {
			printk("Failed to configure button %d (%d)\n", i, ret);
			return 0;
		}

		ret = gpio_pin_interrupt_configure_dt(&buttons[i],
						      GPIO_INT_EDGE_TO_ACTIVE);
		if (ret < 0) {
			printk("Failed to configure IRQ on button %d (%d)\n",
			       i, ret);
			return 0;
		}

		ctx[i].id = i + 1;
		ctx[i].last_press_ms = 0;
		ctx[i].led_on = false;
		ctx[i].blinks_left = 0;

		k_work_init_delayable(&ctx[i].blink, blink_handler);

		gpio_init_callback(&ctx[i].cb, button_pressed,
				   BIT(buttons[i].pin));
		gpio_add_callback(buttons[i].port, &ctx[i].cb);
	}

	printk("Ready. Press button 1..4:\n");
	printk("  button N -> \"hello button N from con 11\" + LED N blinks N times\n");

	return 0;
}