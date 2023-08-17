#include<linux/init.h>
#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/slab.h>
#include<asm/uaccess.h>
#include<linux/fs.h>
#include<linux/errno.h>
#include<linux/device.h>
#include<asm/irq.h>
#include<linux/interrupt.h>
#include<asm/delay.h>
#include<asm/siginfo.h>
#include<linux/pid.h>
#include<linux/uaccess.h>
#include<linux/sched/signal.h>
#include<linux/pid_namespace.h>
#include<linux/gfp.h>
#include<linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/delay.h>

#include <linux/fb.h>
#include <linux/init.h>

#define OUT32(_a,_b)    *((volatile unsigned int*)(_b))=(_a)
#define IN32(_a)        (*(volatile unsigned int*)(_a))
#define LCD_HEIGHT      800
#define LCD_WIDTH       480
#define VIDEOMEM_SIZE LCD_WIDTH * LCD_HEIGHT * 2
#define BUFFER_SIZE     4
#define LCDADDR_RESET       0x00
#define LCDADDR_TEXTCOLOR   0x04
#define LCDADDR_CHAR        0x08
#define LCDADDR_PIXEL       0x0c
#define LCDADDR_CURSOR      0x10
#define LCDADDR_PIXELPOS    0x14
#define LCDADDR_DMAADDR     0x18
#define LCDADDR_DMALEN      0x1c
#define LCDADDR_DMAACK      0x20
#define LCDADDR_TOPLEFT     0x24
#define LCDADDR_BOTTOMRIGHT 0x28

static const char mariverlcd_name[] = "mariverlcd";
unsigned long pseudo_palette[17];


/* This structure will contain fixed information about our hardware. */
static struct fb_fix_screeninfo mariverlcd_fix  = {
		.type        = FB_TYPE_PACKED_PIXELS,
		.visual      = FB_VISUAL_TRUECOLOR, 
		.accel       = FB_ACCEL_NONE, /* Indicate to driver which specific chip/card we have */
		.line_length = LCD_WIDTH * 2, /* length of a line in bytes */
};

/* fb_var_screeninfo is used to describe the features of a video card that are user defined. */
static struct fb_var_screeninfo mariverlcd_var  = {
		.xres        = LCD_WIDTH,
		.yres        = LCD_HEIGHT,
		.xres_virtual    = LCD_WIDTH,
		.yres_virtual    = LCD_HEIGHT,
		.width        = LCD_WIDTH,
		.height        = LCD_HEIGHT,
		.bits_per_pixel = 16,
		.red         = {11, 5, 0},
		.green         = {5, 6, 0},
		.blue         = {0, 5, 0},
		.activate     = FB_ACTIVATE_NOW,
		.vmode     = FB_VMODE_NONINTERLACED,
};

struct mariverlcd {
	struct device *dev;
	struct fb_info *info;
	dma_addr_t videomem;
    void __iomem *dev_base;
    struct timer_list timer;
    int is_busy;
	int is_open;
    int delay;
	struct work_struct wq;
};

#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)

static int mariverlcd_setcolreg(unsigned regno,
		unsigned red, unsigned green, unsigned blue,
		unsigned transp, struct fb_info *info)
{
	int ret = 1;
	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
				7471 * blue) >> 16;
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			u32 value;

			red = CNVT_TOHW(red, info->var.red.length);
			green = CNVT_TOHW(green, info->var.green.length);
			blue = CNVT_TOHW(blue, info->var.blue.length);
			transp = CNVT_TOHW(transp, info->var.transp.length);

			value = (red << info->var.red.offset) |
					(green << info->var.green.offset) |
					(blue << info->var.blue.offset) |
					(transp << info->var.transp.offset);

			pal[regno] = value;
			ret = 0;
		}
		break;
	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		break;
	}
	return ret;
}

int mariverlcd_open(struct fb_info *info, int user){
	struct mariverlcd *item = info->par;
	item->is_open = 1;
	mod_timer(&item->timer, jiffies + item->delay);
	printk(KERN_EMERG "LCD has been open!\n");
	return 0;
}

int mariverlcd_release(struct fb_info *info, int user){
	struct mariverlcd *item = info->par;
	item->is_open = 0;
	printk(KERN_EMERG "LCD has been released!!\n");
	return 0;
}

static struct fb_ops mariverlcd_fbops = {
		.owner        = THIS_MODULE,
		.fb_write     = fb_sys_write,
		.fb_fillrect  = sys_fillrect,
		.fb_copyarea  = sys_copyarea,
		.fb_imageblit = sys_imageblit,
		.fb_setcolreg   = mariverlcd_setcolreg,
		.fb_open    = mariverlcd_open,
		.fb_release = mariverlcd_release,
};


void mariverlcd_do_work(struct work_struct *work){
	disable_irq(5);
    struct mariverlcd *item = container_of(work, struct mariverlcd, wq);
    if(item->is_busy) goto busying; //上一次还没处理完呢！
    
	dma_addr_t dma_addr = item->videomem;
	writel(dma_addr, (item->dev_base + LCDADDR_DMAADDR));
    writel(0, (item->dev_base + LCDADDR_TOPLEFT));
	writel((LCD_WIDTH - 1) << 16 | (LCD_HEIGHT - 1), (item->dev_base + LCDADDR_BOTTOMRIGHT));
    writel(VIDEOMEM_SIZE, (item->dev_base + LCDADDR_DMALEN));

    item->is_busy = 1;
    busying:
	enable_irq(5);
}


/* use timer to refresh... */
static void mariverlcd_timer(struct timer_list *t){
	struct mariverlcd* item = from_timer(item, t, timer);
	schedule_work(&item->wq);
	//耗时的操作，不要写在timer里面！会导致卡顿的！！
    //这里借助工作队列实现异步调度..
}


irqreturn_t mariverlcd_irq(int irq, void *dev_id){

	struct mariverlcd* item = (struct mariverlcd*)dev_id;
    writel(1, (item->dev_base + LCDADDR_DMAACK)); //do_ack
    item->is_busy = 0;
	if(item->is_open){
		mod_timer(&item->timer, jiffies + item->delay);
	}
    return IRQ_HANDLED;
}

static int mariverlcd_probe(struct platform_device *dev){
    int ret = 0;
	struct mariverlcd *item;
	struct fb_info *info;
	unsigned char  *videomemory;
	unsigned int size;
	dma_addr_t map_dma;
    int irq;

    printk(KERN_EMERG "LCD::mariverlcd_probe!, but wait for 1s...\n");
	mdelay(1000);
	
    item = kzalloc(sizeof(struct mariverlcd), GFP_KERNEL);
	if (!item) {
		printk(KERN_ALERT "unable to kzalloc for mariverlcd\n");
		ret = -ENOMEM;
		goto out;
	}
    item->is_busy = 0;
	item->is_open = 0;
    item->delay = msecs_to_jiffies(50); //20Hz
	item->dev = &dev->dev;
	dev_set_drvdata(&dev->dev, item);
	
    info = framebuffer_alloc(0, &dev->dev);
	if (!info) {
		ret = -ENOMEM;
		printk(KERN_ALERT "unable to framebuffer_alloc\n");
		goto out_item;
	}
	item->info = info;
    
    info->par = item;
	info->dev = &dev->dev;
	info->fbops = &mariverlcd_fbops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fix = mariverlcd_fix;
	info->var = mariverlcd_var;
	info->fix.smem_len = VIDEOMEM_SIZE;
	info->pseudo_palette = &pseudo_palette;
	
	size = PAGE_ALIGN(VIDEOMEM_SIZE);
	info->screen_buffer = dma_alloc_wc(item->dev, size,
					   &map_dma, GFP_KERNEL);
	
	if (!info->screen_buffer)
	{
		printk(KERN_ALERT "Can not allocate memory for framebuffer\n");
		ret = -ENOMEM;
		goto out_info;
	}
	memset(info->screen_buffer, 0xff, VIDEOMEM_SIZE);
    info->fix.smem_start =(unsigned long)(map_dma);
    item->videomem = map_dma;
	
    ret = register_framebuffer(info);
	if (ret < 0) {
		printk(KERN_ALERT "unable to register_frambuffer\n");
		goto out_pages;
	}
    
    /* get base addr */
    item->dev_base = devm_platform_ioremap_resource(dev, 0);
    if (IS_ERR(item->dev_base)) {
		ret = PTR_ERR(item->dev_base);
		goto out_pages;
	}
    
    /* get and request irq */
    irq = platform_get_irq(dev, 0);
    if (irq < 0) {
		ret = -ENODEV;
		goto out_pages;
	}

    ret = request_irq(irq, mariverlcd_irq, 0, mariverlcd_name, item);
    if(ret < 0){
        printk(KERN_ALERT "Request irq fail!\n");
        goto out_pages;
    }

	writel(1, (item->dev_base + LCDADDR_RESET));

	INIT_WORK(&item->wq, mariverlcd_do_work);
    timer_setup(&item->timer, mariverlcd_timer, 0);
    item->timer.expires = jiffies + item->delay;
    add_timer(&item->timer);

    return ret;

	out_pages:
	kfree(videomemory);
    out_info:
	framebuffer_release(info);
    out_item:
	kfree(item);
    out:
	return ret;
}

static int mariverlcd_remove(struct platform_device *dev){
    struct fb_info *info = platform_get_drvdata(dev);
	struct mariverlcd *item = (struct mariverlcd *)info->par;
	
	if (info) {
		unregister_framebuffer(info);
		framebuffer_release(info);
		timer_delete(&item->timer);
		if (item->videomem)
		dma_free_wc(item->dev, PAGE_ALIGN(info->fix.smem_len),
			    info->screen_buffer, info->fix.smem_start);
		kfree(item);
	}
	return 0;
}


static const struct of_device_id my_match_table[] = {
	{ .compatible = "mariverlcd" },
	{},
};
MODULE_DEVICE_TABLE(of, my_match_table);


struct platform_driver mariverlcd_driver = {
	.probe = mariverlcd_probe,
	.remove = mariverlcd_remove,

	.driver =
	{
		.name = (char *)mariverlcd_name,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(my_match_table),
	},
};

static struct platform_device *mariverlcd_device;
static int __init mariverlcd_init(void){
    int ret = 0;
	printk("mariverlcd_init\n");

	ret = platform_driver_register(&mariverlcd_driver);
	if (ret) {
		printk(KERN_ALERT "unable to platform_driver_register\n");

	}
	else{
		mariverlcd_device = platform_device_alloc("my_fb_driver", 0);
		if (mariverlcd_device){
			ret = platform_device_add(mariverlcd_device);
			printk("device added\n");
		}
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(mariverlcd_device);
			platform_driver_unregister(&mariverlcd_driver);
		}
	}

	
	printk(KERN_EMERG "LCD Framebuffer::Hello world!\n");
    return ret;
}

module_init(mariverlcd_init);

static void __exit mariverlcd_exit(void) {
	platform_device_unregister(mariverlcd_device);
	platform_driver_unregister(&mariverlcd_driver);
	printk("mariverlcd_exit\n");
}

module_exit(mariverlcd_exit);


EXPORT_SYMBOL(mariverlcd_driver);

MODULE_LICENSE("GPL");