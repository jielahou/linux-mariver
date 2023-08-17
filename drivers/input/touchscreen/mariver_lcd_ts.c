
/*
 * Heavily based on...
 * Fujitsu serial touchscreen driver, imx6ul_tsc and goodix
 */


#include <linux/errno.h>
#include <linux/module.h>

#include <linux/dmi.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <asm/delay.h>
#include <linux/delay.h>
#include <asm/siginfo.h>
#include <linux/pid.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/pid_namespace.h>
#include <linux/timer.h>    
#include "goodix.h"

#define DRIVER_DESC	"Mariverlcd serial touchscreen driver"

#define GOODIX_BUFFER_STATUS_READY	BIT(7)
#define GOODIX_HAVE_KEY			BIT(4)
#define GOODIX_BUFFER_STATUS_TIMEOUT	20

#define OUT32(_a,_b)    *((volatile unsigned int*)(_b))=(_a)
#define IN32(_a)        (*(volatile unsigned int*)(_a))

/*
 * Per-touchscreen data.
 */
struct mariverlcd_ts {
	struct device* dev;
    struct i2c_client * client;
    struct input_dev *input_dev;
    spinlock_t lock;
    struct timer_list timer;
    //int is_busy;
	struct work_struct wq;
};

struct Point{
    int x,y;
};

struct grc_ioctl_info{
    unsigned int addr;
    unsigned int data;
};


struct Point last_pt={
    .x=-1, .y=-1
};

/**
 * goodix_i2c_read - read data from a register of the i2c slave device.
 *
 * @client: i2c device.
 * @reg: the register to read from.
 * @buf: raw write data buffer.
 * @len: length of the buffer to write
 */
int goodix_i2c_read(struct i2c_client *client, u16 reg, u8 *buf, int len)
{
	struct i2c_msg msgs[2];
	//__be16 wbuf = cpu_to_be16(reg);
	u8 *addr_buf;
	int ret;
	
	addr_buf = kmalloc(len + 2, GFP_KERNEL);
	if (!addr_buf)
		return -ENOMEM;
	addr_buf[0] = reg >> 8;
	addr_buf[1] = reg & 0xFF;
	

	msgs[0].flags = 0;
	msgs[0].addr  = client->addr;
	msgs[0].len   = 2;
	msgs[0].buf   = addr_buf;

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].len   = len;
	msgs[1].buf   = buf;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret >= 0)
		ret = (ret == ARRAY_SIZE(msgs) ? 0 : -EIO);

	if (ret)
		dev_err(&client->dev, "Error reading %d bytes from 0x%04x: %d\n",
			len, reg, ret);
	return ret;
}

/**
 * goodix_i2c_write - write data to a register of the i2c slave device.
 *
 * @client: i2c device.
 * @reg: the register to write to.
 * @buf: raw data buffer to write.
 * @len: length of the buffer to write
 */
int goodix_i2c_write(struct i2c_client *client, u16 reg, const u8 *buf, int len)
{
	u8 *addr_buf;
	struct i2c_msg msg;
	int ret;

	addr_buf = kmalloc(len + 2, GFP_KERNEL);
	if (!addr_buf)
		return -ENOMEM;

	addr_buf[0] = reg >> 8;
	addr_buf[1] = reg & 0xFF;
	memcpy(&addr_buf[2], buf, len);

	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = addr_buf;
	msg.len = len + 2;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		ret = (ret == 1 ? 0 : -EIO);

	kfree(addr_buf);

	if (ret)
		dev_err(&client->dev, "Error writing %d bytes to 0x%04x: %d\n",
			len, reg, ret);
	return ret;
}

int goodix_i2c_write_u8(struct i2c_client *client, u16 reg, u8 value)
{
	return goodix_i2c_write(client, reg, &value, sizeof(value));
}


void mariverlcd_ts_do_work(struct work_struct *work){
	struct mariverlcd_ts* ts = container_of(work, struct mariverlcd_ts, wq);
    int error;
	u16 point_addr = 0x8150;

    u8 data[4];
    u8 touch_num;
	spin_lock(&ts->lock);
	//printk(KERN_EMERG "timer 1\n");
    error = goodix_i2c_read(ts->client, GOODIX_READ_COOR_ADDR, data, 1); //read 1 byte
    if (error)
        goto end;

    if (data[0] & GOODIX_BUFFER_STATUS_READY){
        //有数据
        touch_num = data[0] & 0x0f;
        if (touch_num >= 1) {
            error = goodix_i2c_read(ts->client, point_addr, data, 4); //read 1 byte
            if (error)
                goto end;
            input_report_key(ts->input_dev, BTN_TOUCH, 1);
            input_report_abs(ts->input_dev, ABS_X, (data[1] << 8) | data[0]);
            input_report_abs(ts->input_dev, ABS_Y, (data[3] << 8) | data[2]);
            input_sync(ts->input_dev);
			//update
			last_pt.x=(data[1] << 8) | data[0];
            last_pt.y=(data[3] << 8) | data[2];
        } else {
            input_report_key(ts->input_dev, BTN_TOUCH, 0);
			input_sync(ts->input_dev);
			last_pt.x=-1;//???
            last_pt.y=-1;//???
        }
    }
    end:
	spin_unlock(&ts->lock);
    goodix_i2c_write_u8(ts->client, GOODIX_READ_COOR_ADDR, 0);
}


/* 定时查询，上报即可 */
static void mariverlcd_ts_timer(struct timer_list *t){
	struct mariverlcd_ts* ts = from_timer(ts, t, timer);
    
	schedule_work(&ts->wq);

    mod_timer(&ts->timer, jiffies + msecs_to_jiffies(50));
}


int grc_dev_open(struct inode *i_node, struct file *filp){
    //printk(KERN_ALERT "GRC Open!\n");
    return 0;
}

ssize_t grc_dev_write(struct file *filp, const char __user *buf, size_t count, loff_t *off){
    int len=count;
    *off += len;
    return len;
}

int grc_dev_release(struct inode *i_node, struct file *filp){
    return 0;
}

long grc_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    static struct grc_ioctl_info info;
    unsigned long flags=0;
    local_irq_save(flags);
    //disable_irq(4);
    switch(cmd){
        case 235:{  //ack soft irq
            break;
        }
        case 232:{  //read reg
            copy_from_user(&info,(struct grc_ioctl_info*)arg,sizeof(info));
            info.data=IN32(0xbfd00000 | (info.addr & 0xFFFF));
            copy_to_user((struct grc_ioctl_info*)arg,&info,sizeof(info));
            break;
        }
        case 233:{  //write reg
            //printk(KERN_ALERT "GRC write reg\n");
            copy_from_user(&info,(struct grc_ioctl_info*)arg,sizeof(info));
            OUT32(info.data,0xbfd00000 | (info.addr & 0xFFFF));
            break;
        }
        case 234:{  //get intr
            break;
        }
        case 231:{  //set pid
            break;
        }
        case 230:{  //get point
            struct Point pt;
            pt=last_pt;
            last_pt.x=-1;
            last_pt.y=-1;
            copy_to_user((struct Point*)arg,&pt,sizeof(pt));
            break;
        }
        case 229:{  //start touch int
            break;
        }
        default:    enable_irq(4); return -ENOTTY;
    }
    //enable_irq(4);
    local_irq_restore(flags);
    return 0;
}

static int mariverlcd_ts_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
    struct mariverlcd_ts *mariverlcd_ts_info;
	struct input_dev *input_dev;
	int err;

	mariverlcd_ts_info = devm_kzalloc(dev, sizeof(struct mariverlcd_ts), GFP_KERNEL);
	input_dev = devm_input_allocate_device(dev);
	if (!mariverlcd_ts_info || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}
    mariverlcd_ts_info->dev = dev;
    mariverlcd_ts_info->client = client;
    i2c_set_clientdata(client, mariverlcd_ts_info);
    

    mariverlcd_ts_info->input_dev = input_dev;
    spin_lock_init(&mariverlcd_ts_info->lock);
    INIT_WORK(&mariverlcd_ts_info->wq, mariverlcd_ts_do_work);

	input_dev->name = "Mariver Serial Touchscreen";
    input_dev->id.bustype = BUS_I2C;
    

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
    /* input_set_abs_params函数用于设置上报的数值的取值范围。 */
    /* dev --input_dev结构体 | axis --上报的数值 | min --最小值 | max --最大值 | fuzz --数据偏差值 | flat --平滑位置 */
	input_set_abs_params(input_dev, ABS_X, 0, 480, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 864, 0, 0);
	input_set_drvdata(input_dev, mariverlcd_ts_info);

    //reset IIC
    goodix_i2c_write_u8(client, GOODIX_REG_COMMAND, 2);
    usleep_range(5000, 6000);
    goodix_i2c_write_u8(client, GOODIX_REG_COMMAND, 0);


    timer_setup(&mariverlcd_ts_info->timer, mariverlcd_ts_timer, 0);
    mod_timer(&mariverlcd_ts_info->timer, jiffies + msecs_to_jiffies(100));
    printk(KERN_ALERT "Init touch IIC done\n");

    err = input_register_device(mariverlcd_ts_info->input_dev);
	if (err)
		goto fail1;

	//开字符设备
	static struct file_operations fops={
        .owner=THIS_MODULE,
        .open=grc_dev_open,
        .write=grc_dev_write,
        .release=grc_dev_release,
        .unlocked_ioctl=grc_dev_ioctl
    };
    err =register_chrdev(165,"grc",&fops);
    if(err < 0){
        printk(KERN_ALERT "GRC register failed!\n");
        goto fail1;
    }
    printk(KERN_ALERT "GRC register success\n");


    //不用中断喵，不用中断谢谢喵

	return 0;


 fail1:
	input_free_device(input_dev);
	kfree(mariverlcd_ts_info);
	return err;
}


static const struct of_device_id mariverlcd_ts[] = {
	{ .compatible = "mariverlcd_ts", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mariverlcd_ts_match);


static struct i2c_driver mariverlcd_ts_driver = {
	.probe = mariverlcd_ts_probe,
	.driver = {
        .owner = THIS_MODULE,
        .name = "mariverlcd_ts",
        .of_match_table = mariverlcd_ts,
    },
};

module_i2c_driver(mariverlcd_ts_driver);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

