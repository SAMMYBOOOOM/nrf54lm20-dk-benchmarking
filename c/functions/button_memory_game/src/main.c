/*
 * Button memory game (Simon-style) for Zephyr.
 *
 * How to play
 * -----------
 *   1. Press button 0 while the game is idle to start a new game.
 *   2. The board flashes a sequence of LEDs - watch and remember it.
 *   3. Repeat the sequence with the matching buttons, in order.
 *   4. Each level adds one more step to the sequence.
 *   5. A wrong press ends the game and the score is printed on the console.
 *
 * Score = number of levels completed before the mistake.
 *
 * Buttons 0..3 -> LEDs 0..3 (sw0..sw3 / led0..led3 DT aliases).
 * Button 0 doubles as the "start" button while the game is idle;
 * during a game all four buttons (0..3) are valid inputs.
 *
 * Devicetree requirements (aliases):
 *   sw0, sw1, sw2, sw3     -> the four buttons
 *   led0, led1, led2, led3 -> the four LEDs
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define NUM_BUTTONS 4
#define SEQ_MAX 64 /* safety cap on the sequence length */

/* ---- Timing (tweak these to change the difficulty) -------------------- */
#define FLASH_ON_MS 350  /* how long an LED stays lit                */
#define FLASH_OFF_MS 200 /* gap between LEDs while showing sequence  */
#define GAP_MS 600       /* pause before showing / before your turn  */
#define DEBOUNCE_MS 50   /* button debounce window                   */

/* ---- Devicetree aliases ---------------------------------------------- */
#define BTN0_NODE DT_ALIAS(sw0)
#define BTN1_NODE DT_ALIAS(sw1)
#define BTN2_NODE DT_ALIAS(sw2)
#define BTN3_NODE DT_ALIAS(sw3)

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)
#define LED3_NODE DT_ALIAS(led3)

#if !DT_NODE_HAS_STATUS(BTN0_NODE, okay) ||                                    \
    !DT_NODE_HAS_STATUS(BTN1_NODE, okay) ||                                    \
    !DT_NODE_HAS_STATUS(BTN2_NODE, okay) ||                                    \
    !DT_NODE_HAS_STATUS(BTN3_NODE, okay)
#error "This sample needs aliases sw0, sw1, sw2 and sw3 in devicetree."
#endif

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay) ||                                    \
    !DT_NODE_HAS_STATUS(LED1_NODE, okay) ||                                    \
    !DT_NODE_HAS_STATUS(LED2_NODE, okay) ||                                    \
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
  struct gpio_callback cb; /* button interrupt callback */
  int id;                  /* 0..3                      */
  int64_t last_press_ms;   /* debounce timestamp        */
};

static struct btn_ctx ctx[NUM_BUTTONS];

/* ---- Game state ------------------------------------------------------- */
enum game_state {
  GAME_IDLE,    /* waiting for button 0 to start   */
  GAME_SHOWING, /* playing back the sequence       */
  GAME_INPUT,   /* waiting for the player's input  */
  GAME_OVER,    /* score printed, error blink      */
};

static volatile enum game_state state = GAME_IDLE;

static K_SEM_DEFINE(start_sem, 0, 1);

/* Queue of button presses collected in the ISR and consumed by the game
 * thread. A queue (instead of a single variable) avoids dropping presses
 * when the player types faster than the confirmation flashes.
 */
K_MSGQ_DEFINE(input_q, sizeof(int), 16, 4);

static uint8_t sequence[SEQ_MAX];
static int seq_len;

/* ---- Tiny xorshift PRNG (no Kconfig dependencies) --------------------- */
static uint32_t rng_state = 1u;

static uint32_t rng_next(void) {
  uint32_t x = rng_state;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;

  return x;
}

/* ---- LED helpers ------------------------------------------------------ */
static void leds_all_off(void) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    gpio_pin_set_dt(&leds[i], 0);
  }
}

static void flash_led(int idx) {
  gpio_pin_set_dt(&leds[idx], 1);
  k_sleep(K_MSEC(FLASH_ON_MS));
  gpio_pin_set_dt(&leds[idx], 0);
}

static void show_sequence(void) {
  k_sleep(K_MSEC(GAP_MS));

  for (int i = 0; i < seq_len; i++) {
    flash_led(sequence[i]);
    k_sleep(K_MSEC(FLASH_OFF_MS));
  }
}

/* ---- Button interrupt handler (ISR context) -------------------------- */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                           uint32_t pins) {
  struct btn_ctx *c = CONTAINER_OF(cb, struct btn_ctx, cb);
  int64_t now = k_uptime_get();

  ARG_UNUSED(dev);
  ARG_UNUSED(pins);

  /* Simple debounce */
  if ((now - c->last_press_ms) < DEBOUNCE_MS) {
    return;
  }
  c->last_press_ms = now;

  /* Button 0 starts a new game, but only while the game is idle. */
  if (state == GAME_IDLE && c->id == 0) {
    k_sem_give(&start_sem);
    return;
  }

  /* During the input phase every button is a game input. */
  if (state == GAME_INPUT) {
    int id = c->id;

    k_msgq_put(&input_q, &id, K_NO_WAIT);
  }
}

/* ---- Game thread ------------------------------------------------------ */
static void game_thread(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1) {
    int level = 0;
    bool game_over = false;
    int pressed;

    /* Wait for "press button 0 to start". */
    k_sem_take(&start_sem, K_FOREVER);

    /* Throw away any stale presses queued before the game began. */
    while (k_msgq_get(&input_q, &pressed, K_NO_WAIT) == 0) {
    }

    leds_all_off();
    seq_len = 0;

    printk("\n=== New game ===\n");

    while (!game_over) {
      if (seq_len >= SEQ_MAX) {
        printk("Maximum sequence length reached - you win!\n");
        break;
      }

      /* Grow the sequence by one random step. */
      sequence[seq_len] = (uint8_t)(rng_next() % NUM_BUTTONS);
      seq_len++;
      level++;

      printk("Level %d: watch the LEDs (%d step%s)...\n", level, seq_len,
             seq_len == 1 ? "" : "s");

      state = GAME_SHOWING;
      show_sequence();

      printk("Level %d: repeat the sequence with the buttons.\n", level);
      state = GAME_INPUT;

      /* Wait for the player to repeat the whole sequence. */
      for (int i = 0; i < seq_len; i++) {
        k_msgq_get(&input_q, &pressed, K_FOREVER);

        if (pressed != sequence[i]) {
          printk("Wrong! Expected button %d, you pressed %d.\n", sequence[i],
                 pressed);
          game_over = true;
          break;
        }

        flash_led(pressed); /* short confirmation */
      }

      if (!game_over) {
        printk("Correct - level %d complete.\n", level);
        k_sleep(K_MSEC(GAP_MS));
      }
    }

    state = GAME_OVER;

    /* Score = number of levels completed. */
    int score = level - 1;

    if (score < 0) {
      score = 0;
    }

    printk(">>> GAME OVER - your score: %d (reached level %d)\n", score, level);

    /* Error blink: all LEDs flash a few times. */
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < NUM_BUTTONS; j++) {
        gpio_pin_set_dt(&leds[j], 1);
      }
      k_sleep(K_MSEC(120));
      for (int j = 0; j < NUM_BUTTONS; j++) {
        gpio_pin_set_dt(&leds[j], 0);
      }
      k_sleep(K_MSEC(120));
    }

    state = GAME_IDLE;
    printk("Press button 0 to start a new game.\n");
  }
}

K_THREAD_STACK_DEFINE(game_stack, 2048);
static struct k_thread game_tid;

/* ---- Initialisation --------------------------------------------------- */
int main(void) {
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

    ret = gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
      printk("Failed to configure IRQ on button %d (%d)\n", i, ret);
      return 0;
    }

    ctx[i].id = i;
    ctx[i].last_press_ms = 0;

    gpio_init_callback(&ctx[i].cb, button_pressed, BIT(buttons[i].pin));
    ret = gpio_add_callback(buttons[i].port, &ctx[i].cb);
    if (ret < 0) {
      printk("Failed to add callback for button %d (%d)\n", i, ret);
      return 0;
    }
  }

  /* Seed the PRNG with something that varies between boots. */
  rng_state =
      (uint32_t)k_cycle_get_32() ^ (uint32_t)k_uptime_get_32() ^ 0x9E3779B9u;
  if (rng_state == 0u) {
    rng_state = 1u; /* xorshift must never be zero */
  }

  k_thread_create(&game_tid, game_stack, K_THREAD_STACK_SIZEOF(game_stack),
                  game_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0,
                  K_NO_WAIT);

  printk("Memory game ready.\n");
  printk("Press button 0 to start.\n");
  printk("Watch the LED sequence, then repeat it with the buttons.\n");
  printk("A wrong button ends the game and prints your score.\n");

  return 0;
}