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
#define VGA_HEIGHT      300
#define VGA_WIDTH       400
#define VIDEOMEM_SIZE VGA_WIDTH * VGA_HEIGHT * 2
#define VGA_MODE_OFFSET	0x3000
#define VGA_DMAADDR_OFFSET	0x3004
#define VGA_H_OFFSET	0x3014
#define VGA_V_OFFSET	0x3018
#define VGA_DISABLE	0x3024
#define VGA_POS	0x3008 //高8列，低8行
#define VGA_CHAR	0x300c //高8列，低8ascii

#define WHITE_BLACK 0x380000



static const char marivervga_name[] = "marivervga";
unsigned long pseudo_palette_vga[17];


/* This structure will contain fixed information about our hardware. */
static struct fb_fix_screeninfo marivervga_fix  = {
		.type        = FB_TYPE_PACKED_PIXELS,
		.visual      = FB_VISUAL_TRUECOLOR, 
		.accel       = FB_ACCEL_NONE, /* Indicate to driver which specific chip/card we have */
		.line_length = VGA_WIDTH * 2, /* length of a line in bytes */
};

/* fb_var_screeninfo is used to describe the features of a video card that are user defined. */
static struct fb_var_screeninfo marivervga_var  = {
		.xres        = VGA_WIDTH,
		.yres        = VGA_HEIGHT,
		.xres_virtual    = VGA_WIDTH,
		.yres_virtual    = VGA_HEIGHT,
		.width        = VGA_WIDTH,
		.height        = VGA_HEIGHT,
		.bits_per_pixel = 16,
		.red         = {11, 5, 0},
		.green         = {5, 6, 0},
		.blue         = {0, 5, 0},
		.activate     = FB_ACTIVATE_NOW,
		.vmode     = FB_VMODE_NONINTERLACED,
};

struct marivervga {
	struct device *dev;
	struct fb_info *info;
	dma_addr_t videomem;
	struct timer_list timer;
	struct work_struct wq;
	int delay;
    void __iomem *dev_base;
};

#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)

static int marivervga_setcolreg(unsigned regno,
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


int marivervga_open(struct fb_info *info, int user){
	struct marivervga* item = info->par;
	writel(item->videomem, (item->dev_base + VGA_DMAADDR_OFFSET));
	writel(((VGA_WIDTH - 1) << 16) | 0, (item->dev_base + VGA_H_OFFSET));
	writel(((VGA_HEIGHT - 1) << 16) | 0, (item->dev_base + VGA_V_OFFSET));
	writel(1, (item->dev_base + VGA_MODE_OFFSET)); //DMA_MODE
	printk(KERN_EMERG "VGA has been open!\n");
	return 0;
}

int marivervga_release(struct fb_info *info, int user){
	struct marivervga* item = info->par;
	writel(0, (item->dev_base + VGA_MODE_OFFSET)); //CHAR_MODE
	printk(KERN_EMERG "VGA has been released!!\n");
	return 0;
}

static struct fb_ops marivervga_fbops = {
		.owner        = THIS_MODULE,
		.fb_write     = fb_sys_write,
		.fb_fillrect  = sys_fillrect,
		.fb_copyarea  = sys_copyarea,
		.fb_imageblit = sys_imageblit,
		.fb_setcolreg   = marivervga_setcolreg,
		.fb_open	= marivervga_open,
		.fb_release	= marivervga_release,
};


static int marivervga_probe(struct platform_device *dev){
    int ret = 0;
	struct marivervga *item;
	struct fb_info *info;
	unsigned char  *videomemory;
	unsigned int size;
	dma_addr_t map_dma;

    printk(KERN_EMERG "LCD::marivervga_probe!\n");
	
    item = kzalloc(sizeof(struct marivervga), GFP_KERNEL);
	if (!item) {
		printk(KERN_ALERT "unable to kzalloc for marivervga\n");
		ret = -ENOMEM;
		goto out;
	}

	item->dev = &dev->dev;
	item->delay = msecs_to_jiffies(2);
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
	info->fbops = &marivervga_fbops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fix = marivervga_fix;
	info->var = marivervga_var;
	info->fix.smem_len = VIDEOMEM_SIZE;
	info->pseudo_palette = &pseudo_palette_vga;

	
	size = PAGE_ALIGN(VIDEOMEM_SIZE);
	info->screen_buffer = dma_alloc_wc(item->dev, size,
					   &map_dma, GFP_KERNEL);
	
	if (!info->screen_buffer)
	{
		printk(KERN_ALERT "Can not allocate memory for framebuffer\n");
		ret = -ENOMEM;
		goto out_info;
	}
	printk(KERN_EMERG "vga dma_addr:0x%x", map_dma);
	memset(info->screen_buffer, 0xff, VIDEOMEM_SIZE);
    info->fix.smem_start =(unsigned long)(map_dma);
    item->videomem = map_dma;
	memset(info->screen_buffer, 0xff, VIDEOMEM_SIZE); //init white
	
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


	
	//Init
	writel(0, (item->dev_base + VGA_MODE_OFFSET)); //CHAR_MODE
	writel(0, (item->dev_base + VGA_DISABLE));


	printk(KERN_EMERG "vga finish probe\n");

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

static int marivervga_remove(struct platform_device *dev){
    struct fb_info *info = platform_get_drvdata(dev);
	struct marivervga *item = (struct marivervga *)info->par;
	//这块重写...
	if (info) {
		unregister_framebuffer(info);
		framebuffer_release(info);

		if (item->videomem)
		dma_free_wc(item->dev, PAGE_ALIGN(info->fix.smem_len),
			    info->screen_buffer, info->fix.smem_start);
		kfree(item);
	}
	return 0;
}


static const struct of_device_id my_match_table[] = {
	{ .compatible = "marivervga" },
	{},
};
MODULE_DEVICE_TABLE(of, my_match_table);


struct platform_driver marivervga_driver = {
	.probe = marivervga_probe,
	.remove = marivervga_remove,

	.driver =
	{
		.name = (char *)marivervga_name,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(my_match_table),
	},
};

static struct platform_device *marivervga_device;
static int __init marivervga_init(void){
    int ret = 0;
	printk("marivervga_init\n");

	ret = platform_driver_register(&marivervga_driver);
	if (ret) {
		printk(KERN_ALERT "unable to platform_driver_register\n");

	}
	else{
		marivervga_device = platform_device_alloc("my_fb_driver_vga", 1); //???
		if (marivervga_device){
			ret = platform_device_add(marivervga_device);
			printk("device added\n");
		}
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(marivervga_device);
			platform_driver_unregister(&marivervga_driver);
		}
	}

	
	printk(KERN_EMERG "VGA Framebuffer::Hello world!\n");
    return ret;
}

module_init(marivervga_init);

static void __exit marivervga_exit(void) {
	platform_device_unregister(marivervga_device);
	platform_driver_unregister(&marivervga_driver);
	printk("marivervga_exit\n");
}

module_exit(marivervga_exit);



EXPORT_SYMBOL(marivervga_driver);

MODULE_LICENSE("GPL");