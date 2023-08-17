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
#include<linux/input.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/screen_info.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>

#define OUT32(_a,_b)    *((volatile unsigned int*)(_b))=(_a)
#define IN32(_a)        (*(volatile unsigned int*)(_a))

struct vc_data* mrvgacon_getvc(void);

static int translate_key_code(unsigned int key_code){
    switch(key_code){
    case 0x1C:  return KEY_A;
    case 0x32:  return KEY_B;
    case 0x21:  return KEY_C;
    case 0x23:  return KEY_D;
    case 0x24:  return KEY_E;
    case 0x2B:  return KEY_F;
    case 0x34:  return KEY_G;
    case 0x33:  return KEY_H;
    case 0x43:  return KEY_I;
    case 0x3B:  return KEY_J;
    case 0x42:  return KEY_K;
    case 0x4B:  return KEY_L;
    case 0x3A:  return KEY_M;
    case 0x31:  return KEY_N;
    case 0x44:  return KEY_O;
    case 0x4D:  return KEY_P;
    case 0x15:  return KEY_Q;
    case 0x2D:  return KEY_R;
    case 0x1B:  return KEY_S;
    case 0x2C:  return KEY_T;
    case 0x3C:  return KEY_U;
    case 0x2A:  return KEY_V;
    case 0x1D:  return KEY_W;
    case 0x22:  return KEY_X;
    case 0x35:  return KEY_Y;
    case 0x1A:  return KEY_Z;
    case 0x45:  return KEY_0;
    case 0x16:  return KEY_1;
    case 0x1E:  return KEY_2;
    case 0x26:  return KEY_3;
    case 0x25:  return KEY_4;
    case 0x2E:  return KEY_5;
    case 0x36:  return KEY_6;
    case 0x3D:  return KEY_7;
    case 0x3E:  return KEY_8;
    case 0x46:  return KEY_9;
    case 0x0E:  return KEY_GRAVE;
    case 0x4E:  return KEY_MINUS;
    case 0x55:  return KEY_EQUAL;
    case 0x5D:  return KEY_BACKSLASH;
    case 0x66:  return KEY_BACKSPACE;
    case 0x29:  return KEY_SPACE;
    case 0x0D:  return KEY_TAB;
    case 0x58:  return KEY_CAPSLOCK;
    case 0x12:  return KEY_LEFTSHIFT;
    case 0x14:  return KEY_LEFTCTRL;
    case 0x11:  return KEY_LEFTALT;
    case 0x59:  return KEY_RIGHTSHIFT;
    case 0xE014:return KEY_RIGHTCTRL;
    case 0xE011:return KEY_RIGHTALT;
    case 0x5A:  return KEY_ENTER;
    case 0x76:  return KEY_ESC;
    case 0x05:  return KEY_F1;
    case 0x06:  return KEY_F2;
    case 0x04:  return KEY_F3;
    case 0x0C:  return KEY_F4;
    case 0x03:  return KEY_F5;
    case 0x0B:  return KEY_F6;
    case 0x83:  return KEY_F7;
    case 0x0A:  return KEY_F8;
    case 0x01:  return KEY_F9;
    case 0x09:  return KEY_F10;
    case 0x78:  return KEY_F11;
    case 0x07:  return KEY_F12;
    case 0x7E:  return KEY_SCROLLLOCK;
    case 0x54:  return KEY_LEFTBRACE;
    case 0xE070:return KEY_INSERT;
    case 0xE06C:return KEY_HOME;
    case 0xE07D:return KEY_PAGEUP;
    case 0xE071:return KEY_DELETE;
    case 0xE069:return KEY_END;
    case 0xE07A:return KEY_PAGEDOWN;
    case 0xE075:return KEY_UP;
    case 0xE06B:return KEY_LEFT;
    case 0xE072:return KEY_DOWN;
    case 0xE074:return KEY_RIGHT;
    case 0x77:  return KEY_NUMLOCK;
    case 0xE04A:return KEY_KPSLASH;
    case 0x7B:  return KEY_KPMINUS;
    case 0x79:  return KEY_KPPLUS;
    case 0xE05A:return KEY_KPENTER;
    case 0x70:  return KEY_KP0;
    case 0x69:  return KEY_KP1;
    case 0x72:  return KEY_KP2;
    case 0x7A:  return KEY_KP3;
    case 0x6B:  return KEY_KP4;
    case 0x73:  return KEY_KP5;
    case 0x74:  return KEY_KP6;
    case 0x6C:  return KEY_KP7;
    case 0x75:  return KEY_KP8;
    case 0x7D:  return KEY_KP9;
    case 0x5B:  return KEY_RIGHTBRACE;
    case 0x4C:  return KEY_SEMICOLON;
    case 0x52:  return KEY_APOSTROPHE;
    case 0x41:  return KEY_COMMA;
    case 0x49:  return KEY_DOT;
    case 0x4A:  return KEY_SLASH;
    }
    return -1;
}


struct input_dev* kdev;

static void mr_report_key(struct input_dev* kdev,unsigned int key_code){
    int keydown=0;
    int keyval=-1;
    if((key_code & 0xFF000000) == 0xF0000000){
        keydown=0;
        key_code=(key_code >> 16) & 0xFF;
    }else if((key_code & 0xFF000000) == 0xE0000000){
        if((key_code & 0x00FF0000) == 0x00F00000){
            keydown=0;
            key_code=((key_code >> 8) & 0xFF) | 0xE000;
        }else{
            keydown=1;
            key_code=(key_code >> 16) & 0xFFFF;
        }
    }else{
        keydown=1;
        key_code=(key_code >> 24) & 0xFF;
    }
    keyval=translate_key_code(key_code);
    if(keyval != -1){
        input_report_key(kdev,keyval,keydown ? 0x01 : 0x00);
        input_sync(kdev);
    }
}

int mrime_handler(unsigned int key_code);

static irqreturn_t keyboard_interrupt(int irq, void *dev_id){
    unsigned int code,state;
    disable_irq(6);
    state=IN32(0xbfd0102c);
    if(state & 1){
        code=IN32(0xbfd01028);
        OUT32(2,0xbfd01028);
        if(!mrime_handler(code))        //输入法优先处理
            mr_report_key(kdev,code);
    }
    enable_irq(6);
    return IRQ_HANDLED;
}


static int mrkeyboard_init(void){
    int retval=0;
    
    kdev=input_allocate_device();
    if(!kdev){
        printk(KERN_ALERT "Ma-River Keyboard ERROR: Cannot alloc device\n");
        return -ENOMEM;
    }
    kdev->name="Ma-River KeyBoard";
    kdev->evbit[0]=BIT_MASK(EV_KEY);
    
    input_set_capability(kdev, EV_KEY, KEY_ENTER);
    input_set_capability(kdev, EV_KEY, KEY_A);
    input_set_capability(kdev, EV_KEY, KEY_B);
    input_set_capability(kdev, EV_KEY, KEY_C);
    input_set_capability(kdev, EV_KEY, KEY_D);
    input_set_capability(kdev, EV_KEY, KEY_E);
    input_set_capability(kdev, EV_KEY, KEY_F);
    input_set_capability(kdev, EV_KEY, KEY_G);
    input_set_capability(kdev, EV_KEY, KEY_H);
    input_set_capability(kdev, EV_KEY, KEY_I);
    input_set_capability(kdev, EV_KEY, KEY_J);
    input_set_capability(kdev, EV_KEY, KEY_K);
    input_set_capability(kdev, EV_KEY, KEY_L);
    input_set_capability(kdev, EV_KEY, KEY_M);
    input_set_capability(kdev, EV_KEY, KEY_N);
    input_set_capability(kdev, EV_KEY, KEY_O);
    input_set_capability(kdev, EV_KEY, KEY_P);
    input_set_capability(kdev, EV_KEY, KEY_Q);
    input_set_capability(kdev, EV_KEY, KEY_R);
    input_set_capability(kdev, EV_KEY, KEY_S);
    input_set_capability(kdev, EV_KEY, KEY_T);
    input_set_capability(kdev, EV_KEY, KEY_U);
    input_set_capability(kdev, EV_KEY, KEY_V);
    input_set_capability(kdev, EV_KEY, KEY_W);
    input_set_capability(kdev, EV_KEY, KEY_X);
    input_set_capability(kdev, EV_KEY, KEY_Y);
    input_set_capability(kdev, EV_KEY, KEY_Z);
    input_set_capability(kdev, EV_KEY, KEY_0);
    input_set_capability(kdev, EV_KEY, KEY_1);
    input_set_capability(kdev, EV_KEY, KEY_2);
    input_set_capability(kdev, EV_KEY, KEY_3);
    input_set_capability(kdev, EV_KEY, KEY_4);
    input_set_capability(kdev, EV_KEY, KEY_5);
    input_set_capability(kdev, EV_KEY, KEY_6);
    input_set_capability(kdev, EV_KEY, KEY_7);
    input_set_capability(kdev, EV_KEY, KEY_8);
    input_set_capability(kdev, EV_KEY, KEY_9);
    input_set_capability(kdev, EV_KEY, KEY_ESC);
    input_set_capability(kdev, EV_KEY, KEY_MINUS);
    input_set_capability(kdev, EV_KEY, KEY_EQUAL);
    input_set_capability(kdev, EV_KEY, KEY_BACKSPACE);
    input_set_capability(kdev, EV_KEY, KEY_TAB);
    input_set_capability(kdev, EV_KEY, KEY_LEFTBRACE);
    input_set_capability(kdev, EV_KEY, KEY_RIGHTBRACE);
    input_set_capability(kdev, EV_KEY, KEY_LEFTCTRL);
    input_set_capability(kdev, EV_KEY, KEY_SEMICOLON);
    input_set_capability(kdev, EV_KEY, KEY_APOSTROPHE);
    input_set_capability(kdev, EV_KEY, KEY_GRAVE);
    input_set_capability(kdev, EV_KEY, KEY_LEFTSHIFT);
    input_set_capability(kdev, EV_KEY, KEY_BACKSLASH);
    input_set_capability(kdev, EV_KEY, KEY_COMMA);
    input_set_capability(kdev, EV_KEY, KEY_DOT);
    input_set_capability(kdev, EV_KEY, KEY_SLASH);
    input_set_capability(kdev, EV_KEY, KEY_RIGHTSHIFT);
    input_set_capability(kdev, EV_KEY, KEY_LEFTALT);
    input_set_capability(kdev, EV_KEY, KEY_SPACE);
    input_set_capability(kdev, EV_KEY, KEY_CAPSLOCK);
    input_set_capability(kdev, EV_KEY, KEY_F1);
    input_set_capability(kdev, EV_KEY, KEY_F2);
    input_set_capability(kdev, EV_KEY, KEY_F3);
    input_set_capability(kdev, EV_KEY, KEY_F4);
    input_set_capability(kdev, EV_KEY, KEY_F5);
    input_set_capability(kdev, EV_KEY, KEY_F6);
    input_set_capability(kdev, EV_KEY, KEY_F7);
    input_set_capability(kdev, EV_KEY, KEY_F8);
    input_set_capability(kdev, EV_KEY, KEY_F9);
    input_set_capability(kdev, EV_KEY, KEY_F10);
    input_set_capability(kdev, EV_KEY, KEY_NUMLOCK);
    input_set_capability(kdev, EV_KEY, KEY_SCROLLLOCK);
    input_set_capability(kdev, EV_KEY, KEY_KP7);
    input_set_capability(kdev, EV_KEY, KEY_KP8);
    input_set_capability(kdev, EV_KEY, KEY_KP9);
    input_set_capability(kdev, EV_KEY, KEY_KPMINUS);
    input_set_capability(kdev, EV_KEY, KEY_KP4);
    input_set_capability(kdev, EV_KEY, KEY_KP5);
    input_set_capability(kdev, EV_KEY, KEY_KP6);
    input_set_capability(kdev, EV_KEY, KEY_KPPLUS);
    input_set_capability(kdev, EV_KEY, KEY_KP1);
    input_set_capability(kdev, EV_KEY, KEY_KP2);
    input_set_capability(kdev, EV_KEY, KEY_KP3);
    input_set_capability(kdev, EV_KEY, KEY_KP0);
    input_set_capability(kdev, EV_KEY, KEY_KPDOT);
    input_set_capability(kdev, EV_KEY, KEY_F11);
    input_set_capability(kdev, EV_KEY, KEY_F12);
    input_set_capability(kdev, EV_KEY, KEY_KPENTER);
    input_set_capability(kdev, EV_KEY, KEY_RIGHTCTRL);
    input_set_capability(kdev, EV_KEY, KEY_KPSLASH);
    input_set_capability(kdev, EV_KEY, KEY_RIGHTALT);
    input_set_capability(kdev, EV_KEY, KEY_HOME);
    input_set_capability(kdev, EV_KEY, KEY_UP);
    input_set_capability(kdev, EV_KEY, KEY_PAGEUP);
    input_set_capability(kdev, EV_KEY, KEY_LEFT);
    input_set_capability(kdev, EV_KEY, KEY_RIGHT);
    input_set_capability(kdev, EV_KEY, KEY_END);
    input_set_capability(kdev, EV_KEY, KEY_DOWN);
    input_set_capability(kdev, EV_KEY, KEY_PAGEDOWN);
    input_set_capability(kdev, EV_KEY, KEY_INSERT);
    input_set_capability(kdev, EV_KEY, KEY_DELETE);

    retval=input_register_device(kdev);
    if(retval < 0){
        printk(KERN_ALERT "Ma-River KeyBoard ERROR: Register device failed\n");
        return retval;
    }
    printk(KERN_ALERT "Ma-River KeyBoard: Register device success\n");



    OUT32(0x0c,0xbfd01028);

    retval=request_irq(6,keyboard_interrupt,0,"keyboard",NULL);
    if(retval < 0){
        printk(KERN_ALERT "Ma-River KeyBoard ERROR: Cannot request irq\n");
        return retval;
    }
    printk(KERN_ALERT "Ma-River KeyBoard: Request irq success!\n");
    

    return 0;
}

static void mrkeyboard_exit(void){
    printk(KERN_ALERT "Bye\n");
}

module_init(mrkeyboard_init);
module_exit(mrkeyboard_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ywy_c_asm");
MODULE_DESCRIPTION("PS/2 Keyboard controller driver");