#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>

// 定義 GPIO 引腳
#define ADDO_PIN 2  // HX711 的數據輸出引腳
#define ADSK_PIN 3  // HX711 的時鐘輸入引腳

// 校準相關的常數和變量
#define SCALE_FACTOR 421    // 比例因子，用於將原始讀數轉換為重量
static unsigned long auto_tare_offset = 0;  // 自動校準偏移值

// 定時器和重量變量
static struct timer_list weight_timer;
static int weight_grams = 0;
#define MAX_WAIT_COUNT 1000000 // 適當調整這個值

// 從 HX711 讀取原始數據
static unsigned long ReadCount(void)
{
    unsigned long Count = 0;    // 存儲讀取數據的變量
    unsigned char i;
    int wait_count = 0;
    gpio_set_value(ADSK_PIN, 0);    // 將時鐘線設為低電平

    // 等待 HX711 準備好數據
    while(gpio_get_value(ADDO_PIN) && wait_count < MAX_WAIT_COUNT) {
        wait_count++;
        udelay(1);
    }

    if (wait_count >= MAX_WAIT_COUNT) {
        printk(KERN_ERR "HX711 read timeout\n");
        return 0xFFFFFFFF; // 表示讀取失敗
    }

    // 讀取 24 位數據
    for (i = 0; i < 24; i++)
    {
        gpio_set_value(ADSK_PIN, 1);    // 將時鐘線拉高，觸發 HX711 輸出下一位數據
        udelay(1);      // 短暫延遲 1 微秒以確保信號穩定
        Count = Count << 1;     // 將 Count 左移一位，為新的位準備空間
        gpio_set_value(ADSK_PIN, 0);    // 將時鐘線拉低，完成一個時鐘週期
        udelay(1);
        
        if(gpio_get_value(ADDO_PIN)) // 如果 ADDO 線為高電平，則當前位為 1，將其加到 Count 中
            Count++;
    }

    // 再進行一次時鐘脈衝
    gpio_set_value(ADSK_PIN, 1);
    udelay(1);
    Count = Count ^ 0x800000;   // 將結果轉換為二補數格式。這是因為 HX711 輸出的是二補數格式的數據
    gpio_set_value(ADSK_PIN, 0);

    return Count;   // 函數最後返回 Count，這就是從 HX711 讀取到的原始 24 位數據
}

// 將原始讀數轉換為克
static int convert_to_grams(unsigned long count)
{
    long weight;
    // 減去自動校準偏移值
    weight = ((long)count - (long)auto_tare_offset);
    // 應用比例因子(我設為423)並轉換為克
    weight = weight / SCALE_FACTOR;
    
    return (int)weight;
}

// 定時器回調函數，用於定期讀取重量
static void weight_timer_callback(struct timer_list *t)
{
    unsigned long raw_count;

    raw_count = ReadCount();    // 讀取原始數據
    if (raw_count == 0xFFFFFFFF) {
        printk(KERN_ERR "Failed to read from HX711-1\n");   // 讀取失敗
    } else {
        printk(KERN_INFO "Raw count 1: %lu\n", raw_count);    // 監控原始讀數
        weight_grams = convert_to_grams(raw_count);    // 轉換為克
        printk(KERN_INFO "Current weight 1: %d grams\n", weight_grams);    // 轉換後的重量值
        mod_timer(&weight_timer, jiffies + HZ);    // 下一秒再次觸發
    }
}

// 用於在 /proc 文件系統中顯示重量
static int weight_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", weight_grams);
    return 0;
}

// 打開 /proc 文件時調用的函數
static int weight_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, weight_proc_show, NULL);
}

// /proc 文件操作結構
static const struct proc_ops weight_proc_fops = {
    .proc_open = weight_proc_open,
    .proc_read = seq_read,      // 標準讀取
    .proc_lseek = seq_lseek,    // 標準搜尋
    .proc_release = single_release,     // 關閉時釋放資源
};

// 模塊初始化函數
static int __init hx711_init(void)
{
    // 初始化GPIO
    if (gpio_request(ADDO_PIN, "HX711_ADDO") < 0 || gpio_request(ADSK_PIN, "HX711_ADSK") < 0) {
        printk(KERN_ERR "Failed to request GPIO pins\n");
        return -1;
    }
    
    gpio_direction_input(ADDO_PIN);     // 設置數據引腳為輸入
    gpio_direction_output(ADSK_PIN, 0);     // 設置時鐘引腳為輸出，初始為低電平

    // 執行自動校準
    auto_tare_offset = ReadCount();
    printk(KERN_INFO "Auto tare offset: %lu\n", auto_tare_offset);

    // 初始化定時器
    timer_setup(&weight_timer, weight_timer_callback, 0);
    mod_timer(&weight_timer, jiffies + HZ);
    
    // 創建 /proc 文件
    proc_create("weight1", 0, NULL, &weight_proc_fops);

    printk(KERN_INFO "HX711 module 1 initialized\n");
    return 0;
}

// 模塊卸載函數
static void __exit hx711_exit(void)
{
    del_timer(&weight_timer);
    gpio_free(ADDO_PIN);
    gpio_free(ADSK_PIN);
    remove_proc_entry("weight1", NULL);     // 移除 /proc 文件
    printk(KERN_INFO "HX711 module 1 removed\n");
}

module_init(hx711_init);
module_exit(hx711_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Beck");
MODULE_DESCRIPTION("HX711 weight sensor 1 driver");