/*
 * pic.c
 *
 *  Created on: 2017年5月16日
 *      Author: zhengxiaolin
 */

#include <pic.h>

/**
 * 初始化8259A芯片
 */
void pic_init()
{
	outb(0x21, 0xFF);		//填上mask
	outb(0xA1, 0xFF);

	outb(0x20, 0x11);		//ICW1
	outb(0x21, 32);			//ICW2
	outb(0x21, 0x04);		//ICW3
	outb(0x21, 0x03);		//ICW4

	outb(0xA0, 0x11);
	outb(0xA1, 32+8);
	outb(0xA1, 2);			//选定软连接主片2号接口
	outb(0xA1, 0x03);

	outb(0x21, 0x00);		//解除mask
	outb(0xA1, 0x00);

	sti();		//这时要打开中断了！！
}

void sti()
{
	asm volatile ("sti");
}

void cli()
{
//	asm volatile ("sti");		//妈的！！！气死我了FUUUUUUUUKKKKKKKKKKKKKKKK！！！！！！！！！！两天时间啊！！！！
								//如果不关闭中断(原子)的话，后边各种问题.....唉  好在最终还发现了。
								//然而发现最终bug竟然特么是这个，一点成就感都没有啊......
	asm volatile ("cli");
}
