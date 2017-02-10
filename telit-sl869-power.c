/*
 * Copyright 2016 Touchstar Technologies Ltd
 *
 * This driver provides a sysfs entry to safely enable and disable power to
 * the Telit SL869 GPS module, preventing it from entering a failed state which
 * requires a full power cycle to recover.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

struct sl869_power {
	struct device *dev;
	struct gpio_desc *vbatt_enable;
	struct gpio_desc *power_enable;
	bool enabled;
};

static void power_set(struct sl869_power *power, bool set)
{
	dev_dbg(power->dev, "set enabled from %d to %d.\n", power->enabled,
		set);

	if (power->enabled == set)
		return;

	gpiod_set_value_cansleep(power->power_enable, set);
	power->enabled = set;
}

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sl869_power *power = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", power->enabled);
}

static ssize_t enabled_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct sl869_power *power = dev_get_drvdata(dev);
	ssize_t ret;
	long enabled;

	ret = kstrtol(buf, 0, &enabled);
	if (ret != 0)
		return ret;

	power_set(power, !!enabled);
	return size;
}

static DEVICE_ATTR(enabled, S_IRUGO | S_IWUSR, enabled_show, enabled_store);

static struct attribute *sl869_power_attrs[] = {
	&dev_attr_enabled.attr,
	NULL
};

static const struct attribute_group sl869_power_group = {
	.attrs = sl869_power_attrs,
};

static int sl869_power_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int gpio, ret;
	struct sl869_power *power;
	enum of_gpio_flags flags;

	power = devm_kzalloc(&pdev->dev, sizeof(*power), GFP_KERNEL);
	if (!power)
		return -ENOMEM;

	platform_set_drvdata(pdev, power);
	power->dev = &pdev->dev;

	gpio = of_get_named_gpio_flags(np, "vbatt-enable", 0, &flags);
	if (!gpio_is_valid(gpio)) {
		if (gpio != -EPROBE_DEFER)
			dev_err(&pdev->dev, "unable to find vbatt-enable.\n");
		return gpio;
	}

	if (flags & OF_GPIO_ACTIVE_LOW)
		flags = GPIOF_ACTIVE_LOW | GPIOF_OUT_INIT_HIGH;
	else
		flags = GPIOF_OUT_INIT_LOW;

	ret = devm_gpio_request_one(&pdev->dev, gpio, flags,
				    "sl869 vbatt_enable");
	if (ret) {
		dev_err(&pdev->dev, "failed to request vbatt_enable: %d\n",
			ret);
		return -ENODEV;
	}

	power->vbatt_enable = gpio_to_desc(gpio);
	dev_dbg(power->dev, "vbatt-enable is %d.\n", gpio);

	gpio = of_get_named_gpio_flags(np, "power-enable", 0, &flags);

	if (flags & OF_GPIO_ACTIVE_LOW)
		flags = GPIOF_ACTIVE_LOW | GPIOF_OUT_INIT_HIGH;
	else
		flags = GPIOF_OUT_INIT_LOW;

	ret = devm_gpio_request_one(&pdev->dev, gpio, flags,
				    "sl869 power_enable");
	if (ret) {
		dev_err(&pdev->dev, "failed to request power_enable: %d\n",
			ret);
		return -ENODEV;
	}

	power->power_enable = gpio_to_desc(gpio);

	/* If the 1 second power up sequence is interrupted the device can go in
	 * to a failed state which requires a complete power cycle to come out
	 * of.
	 */
	gpiod_set_value_cansleep(power->vbatt_enable, 1);
	msleep(1000);

	ret = sysfs_create_group(&pdev->dev.kobj, &sl869_power_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs entries: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int sl869_power_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &sl869_power_group);
	return 0;
}

static int sl869_power_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sl869_power *power = platform_get_drvdata(pdev);

	power_set(power, 0);
	return 0;
}

static int sl869_power_resume(struct platform_device *pdev)
{
	struct sl869_power *power = platform_get_drvdata(pdev);

	power_set(power, 1);
	return 0;
}

static const struct of_device_id sl869_power_of_match[] = {
	{
		.compatible = "telit,sl869-power"
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, sl869_power_of_match);

static struct platform_driver sl869_power_driver = {
	.probe		= sl869_power_probe,
	.remove		= sl869_power_remove,
	.suspend	= sl869_power_suspend,
	.resume		= sl869_power_resume,
	.driver		= {
		.name		= "sl869-power",
		.of_match_table	= sl869_power_of_match,
	},
};
module_platform_driver(sl869_power_driver);

MODULE_AUTHOR("Steven Jackson <sj@oscode.net>");
MODULE_DESCRIPTION("Power management for Telit SL869");
MODULE_LICENSE("GPL");
