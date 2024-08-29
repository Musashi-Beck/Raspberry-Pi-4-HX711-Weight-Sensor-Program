#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>

#define ADDO_PIN 17
#define ADSK_PIN 27

// 校準相關的常數和變量
#define SCALE_FACTOR 426 // 比例因子，需要根據實際情況調整
static unsigned long auto_tare_offset = 0; // 自動校準偏移值

static struct timer_list weight_timer;
static int weight_grams = 0;
#define MAX_WAIT_COUNT 1000000 // 適當調整這個值

static unsigned long ReadCount(void)
{
    unsigned long Count = 0;
    unsigned char i;
    int wait_count = 0;
    gpio_set_value(ADSK_PIN, 0);
    
    while(gpio_get_value(ADDO_PIN) && wait_count < MAX_WAIT_COUNT) {
        wait_count++;
        udelay(1);
    }

    if (wait_count >= MAX_WAIT_COUNT) {
        printk(KERN_ERR "HX711 read timeout\n");
        return 0xFFFFFFFF; // 表示讀取失敗
    }

    for (i = 0; i < 24; i++)
    {
        gpio_set_value(ADSK_PIN, 1);
        udelay(1);  // 短暫延遲以確保信號穩定
        Count = Count << 1;
        gpio_set_value(ADSK_PIN, 0);
        udelay(1);
        
        if(gpio_get_value(ADDO_PIN))
            Count++;
    }

    gpio_set_value(ADSK_PIN, 1);
    udelay(1);
    Count = Count ^ 0x800000;
    gpio_set_value(ADSK_PIN, 0);

    return Count;
}

static int convert_to_grams(unsigned long count)
{
    long weight;
    
    weight = ((long)count - (long)auto_tare_offset);
    weight = weight / SCALE_FACTOR;
    
    return (int)weight;
}

static void weight_timer_callback(struct timer_list *t)
{
    unsigned long raw_count;

    raw_count = ReadCount();
    if (raw_count == 0xFFFFFFFF) {
        printk(KERN_ERR "Failed to read from HX711-2\n");
    } else {
        printk(KERN_INFO "Raw count 2: %lu\n", raw_count);
        weight_grams = convert_to_grams(raw_count);
        printk(KERN_INFO "Current weight 2: %d grams\n", weight_grams);
        mod_timer(&weight_timer, jiffies + HZ);
    }
}

static int weight_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", weight_grams);
    return 0;
}

static int weight_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, weight_proc_show, NULL);
}

static const struct proc_ops weight_proc_fops = {
    .proc_open = weight_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init hx711_init(void)
{
    if (gpio_request(ADDO_PIN, "HX711_ADDO") < 0 || gpio_request(ADSK_PIN, "HX711_ADSK") < 0) {
        printk(KERN_ERR "Failed to request GPIO pins\n");
        return -1;
    }
    
    gpio_direction_input(ADDO_PIN);
    gpio_direction_output(ADSK_PIN, 0);

    auto_tare_offset = ReadCount();
    printk(KERN_INFO "Auto tare offset: %lu\n", auto_tare_offset);

    timer_setup(&weight_timer, weight_timer_callback, 0);
    mod_timer(&weight_timer, jiffies + HZ);
    
    proc_create("weight2", 0, NULL, &weight_proc_fops);

    printk(KERN_INFO "HX711 module 2 initialized\n");
    return 0;
}

static void __exit hx711_exit(void)
{
    del_timer(&weight_timer);
    gpio_free(ADDO_PIN);
    gpio_free(ADSK_PIN);
    remove_proc_entry("weight1", NULL);
    printk(KERN_INFO "HX711 module 2 removed\n");
}

module_init(hx711_init);
module_exit(hx711_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Beck");
MODULE_DESCRIPTION("HX711 weight sensor 2 driver");