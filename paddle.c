/*
 * BaconBits paddle game controller input driver
 *
 * Copyright 2018 Konsulko Group
 * Matt Porter <mporter@konsulko.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

struct paddle {
	struct input_polled_dev *poll_dev;
	char phys[32];
	struct gpio_desc *button;
	struct iio_channel *channel;
};

static void paddle_poll(struct input_polled_dev *dev)
{
	struct paddle *p = dev->private;
	int ret, a_val, x_val;

	a_val = gpiod_get_value(p->button);
	input_report_key(dev->input, BTN_A, a_val);

	ret = iio_read_channel_raw(p->channel, &x_val);
	if (unlikely(ret < 0))
		return;
	input_report_abs(dev->input, ABS_X, x_val);

	input_sync(dev->input);
}

static int paddle_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct paddle *p;
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	enum iio_chan_type type;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->button = devm_gpiod_get(dev, "button", GPIOD_IN);
	if (IS_ERR(p->button)) {
		ret = PTR_ERR(p->button);
		dev_err(dev, "failed to get button GPIO: %d\n", ret);
		return ret;
	}

	p->channel = devm_iio_channel_get(dev, "thumbwheel");
	if (IS_ERR(p->channel))
		return PTR_ERR(p->channel);

	if (!p->channel->indio_dev)
		return -ENXIO;

	ret = iio_get_channel_type(p->channel, &type);
	if (ret < 0)
		return ret;

	if (type != IIO_VOLTAGE) {
		dev_err(dev, "not voltage channel %d\n", type);
		return -EINVAL;
	}

	poll_dev = devm_input_allocate_polled_device(dev);
	if (!poll_dev) {
		dev_err(dev, "unable to allocate input device\n");
		return -ENOMEM;
	}

	poll_dev->poll_interval = 50;
	poll_dev->poll = paddle_poll;
	poll_dev->private = p;

	p->poll_dev = poll_dev;
	platform_set_drvdata(pdev, p);

	input = poll_dev->input;
	input->name = pdev->name;
	sprintf(p->phys, "paddle/%s", input->dev.kobj.name);
	input->phys = p->phys;
	input->id.bustype = BUS_HOST;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_A, input->keybit);
	__set_bit(EV_ABS, input->evbit);
	/* Hardcode min/max to the resolution of the 12-bit TSCADC */
	input_set_abs_params(input, ABS_X, 0, 4095, 0, 0);

	ret = input_register_polled_device(poll_dev);
	if (ret) {
		dev_err(dev, "unable to register input device: %d\n", ret);
		return ret;
	};

	return 0;
}

static int paddle_remove(struct platform_device *pdev)
{
	struct paddle *p = platform_get_drvdata(pdev);

	input_unregister_polled_device(p->poll_dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id paddle_of_match[] = {
	{ .compatible = "e-ale,baconbits-paddle", },
	{ }
};
MODULE_DEVICE_TABLE(of, paddle_of_match);
#endif

static struct platform_driver __refdata paddle_driver = {
	.driver = {
		.name = "paddle",
		.of_match_table = of_match_ptr(paddle_of_match),
	},
	.probe = paddle_probe,
	.remove = paddle_remove,
};
module_platform_driver(paddle_driver);

MODULE_AUTHOR("Matt Porter");
MODULE_DESCRIPTION("BaconBits paddle game controller input driver");
MODULE_LICENSE("GPL v2");
