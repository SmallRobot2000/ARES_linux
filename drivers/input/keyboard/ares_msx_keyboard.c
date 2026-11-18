#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#define ARES_KBD_STATUS      0x00
#define ARES_KBD_EVENT       0x04
#define ARES_KBD_LED         0x08

#define STATUS_EVENT         BIT(0)

#define EVENT_KEY_MASK       0xff
#define EVENT_PRESSED        BIT(15)

#define LED_CAPS             BIT(0)
#define LED_CODE             BIT(1)

struct ares_kbd {
    void __iomem *base;
    struct input_dev *input;
    u32 led_state;
};


static const unsigned short ares_keymap[88] = {

    /* row 0 */
    KEY_0, KEY_1, KEY_2, KEY_3,
    KEY_4, KEY_5, KEY_6, KEY_7,

    /* row 1 */
    KEY_8, KEY_9, KEY_MINUS, KEY_EQUAL,
    KEY_BACKSLASH, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_SEMICOLON,

    /* row 2 */
    KEY_APOSTROPHE, KEY_GRAVE, KEY_COMMA, KEY_DOT,
    KEY_SLASH, KEY_RIGHTCTRL, KEY_A, KEY_B,

    /* row 3 */
    KEY_C, KEY_D, KEY_E, KEY_F,
    KEY_G, KEY_H, KEY_I, KEY_J,

    /* row 4 */
    KEY_K, KEY_L, KEY_M, KEY_N,
    KEY_O, KEY_P, KEY_Q, KEY_R,

    /* row 5 */
    KEY_S, KEY_T, KEY_U, KEY_V,
    KEY_W, KEY_X, KEY_Y, KEY_Z,

    /* row 6 */
    KEY_LEFTSHIFT, KEY_LEFTCTRL, KEY_LEFTALT, KEY_CAPSLOCK,
    KEY_FN, KEY_F1, KEY_F2, KEY_F3,

    /* row 7 */
    KEY_F4, KEY_F5, KEY_ESC, KEY_TAB,
    KEY_END, KEY_BACKSPACE, KEY_LEFTMETA, KEY_ENTER,  //KEY_LEFTMETA altho the phisical key is on the right

    /* row 8 */
    KEY_SPACE, KEY_HOME, KEY_INSERT, KEY_DELETE,
    KEY_LEFT, KEY_UP, KEY_DOWN, KEY_RIGHT,


    /* remaining entries are reserved */
    [72 ... 87] = KEY_RESERVED,
};


static irqreturn_t ares_kbd_irq(int irq, void *data)
{
    struct ares_kbd *kbd = data;
    bool got_event = false;

    while (readl(kbd->base + ARES_KBD_STATUS) & STATUS_EVENT) {
        u32 event;
        unsigned int id;
        unsigned int code;
        bool pressed;

        /*
         * Reading EVENT pops one FIFO entry.
         */
        event = readl(kbd->base + ARES_KBD_EVENT);

        id = event & EVENT_KEY_MASK;
        pressed = !!(event & EVENT_PRESSED);

        if (id >= ARRAY_SIZE(ares_keymap))
            continue;

        code = ares_keymap[id];

        if (code == KEY_RESERVED)
            continue;

        input_report_key(kbd->input, code, pressed);

        got_event = true;
    }

    if (got_event)
        input_sync(kbd->input);

    return IRQ_HANDLED;
}

static int ares_kbd_event(struct input_dev *input,
                          unsigned int type,
                          unsigned int code,
                          int value)
{
    struct ares_kbd *kbd = input_get_drvdata(input);

    if (type != EV_LED)
        return -EINVAL;

    switch (code) {
    case LED_CAPSL:
        if (value)
            kbd->led_state &= ~LED_CAPS;
        else
            kbd->led_state |= LED_CAPS;
        break;

    case LED_KANA:
        if (value)
            kbd->led_state &= ~LED_CODE;
        else
            kbd->led_state |= LED_CODE;
        break;

    default:
        return -EINVAL;
    }

    writel(kbd->led_state,
           kbd->base + ARES_KBD_LED);

    return 0;
}

static int ares_kbd_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct ares_kbd *kbd;
    struct input_dev *input;
    int irq;
    int ret;
    int i;

    kbd = devm_kzalloc(dev, sizeof(*kbd), GFP_KERNEL);
    if (!kbd)
        return -ENOMEM;

    kbd->base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(kbd->base))
        return PTR_ERR(kbd->base);

    irq = platform_get_irq(pdev, 0);
    if (irq < 0)
        return irq;

    input = devm_input_allocate_device(dev);
    if (!input)
        return -ENOMEM;

    kbd->input = input;

    input->name = "ARES MSX Keyboard";
    input->phys = "ares-msx/input0";

    input->id.bustype = BUS_HOST;

    input->event = ares_kbd_event;

    input_set_drvdata(input, kbd);

    /*
     * Keyboard events.
     */
    __set_bit(EV_KEY, input->evbit);

    /*
     * Let Linux provide keyboard autorepeat.
     */
    __set_bit(EV_REP, input->evbit);

    /*
     * LED support.
     */
    __set_bit(EV_LED, input->evbit);
    __set_bit(LED_CAPSL, input->ledbit);
    __set_bit(LED_KANA, input->ledbit);

    /*
     * Advertise all keys contained in our keymap.
     */
    for (i = 0; i < ARRAY_SIZE(ares_keymap); i++) {
        if (ares_keymap[i] != KEY_RESERVED)
            __set_bit(ares_keymap[i], input->keybit);
    }

    ret = input_register_device(input);
    if (ret)
        return ret;

    ret = devm_request_irq(dev,
                           irq,
                           ares_kbd_irq,
                           0,
                           dev_name(dev),
                           kbd);
    if (ret)
        return ret;

    platform_set_drvdata(pdev, kbd);

    dev_info(dev,
             "ARES MSX keyboard registered, IRQ %d\n",
             irq);

    return 0;
}


static const struct of_device_id ares_kbd_of_match[] = {
    {
        .compatible = "ares,msx-keyboard",
    },
    { }
};

MODULE_DEVICE_TABLE(of, ares_kbd_of_match);

static struct platform_driver ares_kbd_driver = {
    .probe = ares_kbd_probe,

    .driver = {
        .name = "ares-msx-keyboard",
        .of_match_table = ares_kbd_of_match,
    },
};

module_platform_driver(ares_kbd_driver);

MODULE_AUTHOR("Lovro L.");
MODULE_DESCRIPTION("ARES MSX matrix keyboard driver");
MODULE_LICENSE("GPL");