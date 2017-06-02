/*
 * pmm.c
 *
 *  Created on: 2017年5月23日
 *      Author: zhengxiaolin
 */

#include <pmm.h>

struct e820map *memmap = (struct e820map *) ( 0x8000 + VERTUAL_MEM );

struct free_area free_pages;

struct Page *alloc_page(int n)
{
	if(n > free_pages.free_page_num)	return NULL;
	struct list_node *ptr = &free_pages.head;
	while(ptr->next != &free_pages.head){
		struct Page *page = GET_OUTER_STRUCT_PTR(free_pages.head.next, struct Page, node);
		if(page->free_pages >= n){
			int remain_blocks = page->free_pages - n;
			struct list_node *del = &page->node;
			struct list_node *del_next = del->next;
			for(int i = 0; i < n; i ++){
				list_delete(del);
				del = del_next;
				del_next = del_next->next;
			}
			GET_OUTER_STRUCT_PTR(del, struct Page, node)->free_pages = remain_blocks;	//更新split之后的剩下的pages数目
			GET_OUTER_STRUCT_PTR(del, struct Page, node)->flags = 1;	//设为已用
			free_pages.free_page_num -= n;
			return (struct Page *)((u32)page + VERTUAL_MEM);		//想了想，还是返回va更好。
		}
		ptr = ptr->next;
	}
	return NULL;
}

void free_page(struct Page *page, int n)
{
	struct list_node *ptr = &free_pages.head;
	//需要遍历了。因为必须索引到我们的page要插入的位置才行。因为中间位置也有可能会被alloc page。
	struct Page *target = 0;
	while(ptr->next != &free_pages.head){
		target = GET_OUTER_STRUCT_PTR(ptr->next, struct Page, node);
		if(target > page)	break;
		ptr = ptr->next;
	}
	if(target == NULL)	return;
	for(int i = 0; i < n; i ++){
		list_insert_before(&target->node, &(page + i)->node);
	}
	page->flags = 0;
	page->free_pages = n;

	free_pages.free_page_num += n;

	//向下合并
	if(page + n == target){	//如果page和target之间是连续内存的话，合并。
		page->free_pages += target->free_pages;
		target->free_pages = 0;
	}

	//向上合并
	ptr = page->node.prev;
	target = GET_OUTER_STRUCT_PTR(ptr, struct Page, node);
	if(target + target->free_pages == page){
		target->free_pages += page->free_pages;
		page->free_pages = 0;
	}

}

void print_memory()
{
	for(int i = 0; i < memmap->num; i ++){
		printf("base_address: %x  length: %x  type: %d\n", memmap->map[i].base_lo, memmap->map[i].length_lo, memmap->map[i].type);
	}
}

//copy from hx_kernel
void page_fault(struct idtframe *frame)
{
    u32 cr2;
    asm volatile ("mov %%cr2, %0" : "=r" (cr2));

    printf("Page fault at EIP %x, virtual faulting address %x\n", frame->eip, cr2);
    printf("Error code: %x\n", frame->errorCode);

    // bit 0 为 0 指页面不存在内存里
    if (frame->errorCode & 0x4) {
        printf("In user mode.\n");
    } else {
        printf("In kernel mode.\n");
    }
    // bit 1 为 0 表示读错误，为 1 为写错误
    if (frame->errorCode & 0x2) {
        printf("Write error.\n");
    } else {
        printf("Read error.\n");
    }

    switch (frame->errorCode & 0x3) {
    	case 0:
    		//read一个not present的页面，  这样应该从磁盘拿来新的页面
    		do_swap(cr2);
    		break;
    	case 1:
    		//read一个present的页面 竟然还能出错？
    		if((frame->errorCode & 0x4) == 1){
    			//此处查阅资料，我在这里添加了U/S保护的确认。
    			//如果errorCode & 0x1 == 1 则是：由于保护特权级别太高，造成无法读取，进而把bit1归0显示读错误。
    			//stackover flow: https://stackoverflow.com/questions/9759439/page-fault-shortage-of-page-or-access-violation
    			printf("The privilege of this page is so high～, you read it in user mode.\n");
    			while(1);		//宕机
    		}else{
    			printf("cannot be there...\n");
    			while(1);
    		}
    		break;
    	case 2:
    		//write一个not present的页面， 这样应该从磁盘拿来新的页面
    		do_swap(cr2);
    		break;
    	case 3:
    		//write一个present的页面，页保护异常。这样应该COW。

    		//具体实现先行搁置吧。？？？？？？？？？？？？？？？？？？？？？？？？？？？

    		if((frame->errorCode & 0x4) == 1){
    			printf("The privilege of this page is so high～, you read it in user mode.\n");
    			while(1);
    		}

    		break;
    }

    // bit 3 为 1 表示错误是由保留位覆盖造成的
    if (frame->errorCode & 0x8) {
        printf("Reserved bits being overwritten.\n");
    }
    // bit 4 为 1 表示错误发生在取指令的时候
    if (frame->errorCode & 0x10) {
        printf("The fault occurred during an instruction fetch.\n");
    }

    printf("esp: %x\n", frame->esp);

}

void do_swap(u32 cr2)
{
	extern struct mm_struct *mm;
	printf("swap...\n");
}

void pmm_init()
{
	set_handler(14, page_fault);		//设置页异常中断函数
	print_memory();
	page_init();
}

//不能这样。这样的话，这个“页目录表”是在内核中的。但是由于新的页表用于内存分配，即全是“空闲的”，因此必然会把内核的那一页空间置位P位为0不让分配出去。这样，内核不允许空闲页表读取了，
//从而也就不会被malloc分配出去了。但是！因为如果这样定义，页目录表自身在内核中！如果关闭了内核页，那么必然会造成页目录表无法读取！那么内存分配必然会崩溃了。这是一个矛盾的问题。
//因而ucore聪明地把所有内核外的空闲页取出第一页来当成页目录表，而且P位设置可用为1.这样可以读取了。但是要禁止把它分配出去。
//struct pde_t new_page_dir_t[PAGE_SIZE/sizeof(struct pde_t)] __attribute__((aligned(PAGE_SIZE)));	//页目录占用一页4096B，每项4B，共计1024个页目录项(2^10)。每个页目录项管理1024个页表项(2^10)，每个页表占4096B(2^12)，因此能够管理4G内存。
//struct pte_t new_page_kern_table[2 * PAGE_SIZE / sizeof(struct pte_t)] __attribute__((aligned(PAGE_SIZE)));	//假设我们的内核只有8个M。两页就能放下。
//struct pte_t new_page_freemem_table[PAGE_DIR_NUM * PAGE_SIZE / sizeof(struct pte_t)] __attribute__((aligned(PAGE_SIZE)));	//128*1024 也就是说，一共128个页表。
struct pte_t new_page_freemem_table[MAX_PAGE_NUM] __attribute__((aligned(PAGE_SIZE)));	//128*1024 也就是说，一共128个页表。

struct Page *pages;		//pages的起始位置
u32 pt_begin;			//空闲空间的起始位置

void page_init()
{

	for(int i = 0; i < memmap->num; i ++){
		if(memmap->map[i].type == 1 && i != 0){		//找到并非第一个空间
			extern u8 kern_end[];
			u32 free_mem_begin = (u32)kern_end - VERTUAL_MEM;
			u32 free_mem_end = memmap->map[i].base_lo + memmap->map[i].length_lo;
			printf("free_mem_begin:===%x===, free_mem_end:===%x===\n", free_mem_begin, free_mem_end);
			pages = (struct Page *)ROUNDUP(free_mem_begin);		//pages的开始位置
			u32 pt_end = ROUNDDOWN(free_mem_end);				//空闲页的结束位置
			int free_page_num = (pt_end - (u32)pages)/PAGE_SIZE;
			if(free_page_num > MAX_PAGE_NUM) free_page_num = MAX_PAGE_NUM;	//最多3w个页。
//			for(int i = 0; i < free_page_num; i ++){		//计算结果可能会与实际有出入。
//					//这是Page结构体的初始化。
//			}
			pt_begin = ROUNDUP((u32)pages + free_page_num * sizeof(struct Page));				//空闲页的起始位置

			printf("=====%x=====\n", pt_begin);//0x1af000		//fuck 调了好长时间，发现是宏的问题。
								//举个栗子：比如我的原先的ROUNDUP函数，如果输入带+号的算式进去，比如ROUNDUP(printf("%x\n", ROUNDUP((unsigned)0x125000 + 30000 * 20));)
								//会可气地展开成：((unsigned)0x125000 + 30000 * 20 - ((unsigned)0x125000 + 30000 * 20 % 4096) + 4096)......
								//一定要记得加括号......

			//初始化数据结构
			list_init(&free_pages.head);

			memset((void *)pages, 0, free_page_num * sizeof(struct Page));	//清空所有Page结构体信息
			pages->free_pages = free_page_num;
			free_pages.free_page_num = free_page_num;
			for(int i = 0; i < free_page_num; i ++){
				list_insert_before(&free_pages.head, &(pages + i)->node);
			}


			//重新分页!
			extern struct pde_t *pd;
			extern struct pte_t *snd;
			//原先已经有过的，保持原样就可以，不用在设置了。
			//把内核结束开始的新一页(free_mem page空间)到所有空闲结束的所有映射。	//原先0xC0000000~0xC0400000的已经全部映射了。我们从0xC0400000开始吧。
			for(int i = ((u32)(pt_begin + VERTUAL_MEM) >> 22) + 1; i < ((u32)(pt_end + VERTUAL_MEM) >> 22); i ++){
				pd[i].pt_addr = ((u32)&new_page_freemem_table[PAGE_SIZE * i] - VERTUAL_MEM) >> 12;
				pd[i].os = 0;
				pd[i].sign = 0x07;
			}
			for(int i = 0; i < MAX_PAGE_NUM; i ++){
				new_page_freemem_table[i].page_addr = i + 1024;
				new_page_freemem_table[i].os = 0;
				new_page_freemem_table[i].sign = 0x03;
			}
			//设置页目录表
			asm volatile ("movl %0, %%cr3"::"r"(pd));
		}
	}
}

//通过一个page结构体指针算出此页的la
u32 pg_to_addr(struct Page *page)
{
	return (sizeof(struct Page) * (page - pages)) * PAGE_SIZE + pt_begin + VERTUAL_MEM;
}

//通过la算出此页的Page *结构体
struct Page *addr_to_pg(u32 addr)
{
	return (struct Page *)(((addr - VERTUAL_MEM - pt_begin) / PAGE_SIZE) / sizeof(struct Page) + pages);
}

//得到pte指针。如果pde中没有设置的话，就设置pde。
struct pte_t *get_pte(struct pde_t *pde, u32 la, int is_create)
{
	if((pde[la >> 22].sign & 0x1) == 0){		//说明查询的va地址不对应任何pde页目录表项。
		if(!is_create)	return NULL;
		struct Page *pg;
		if((pg = alloc_page(1)) == NULL)	return NULL;	//二级页表已经设置完毕，无需在设置。
		//设置页目录表		//注意页表早已设置好了。无需要再设。
		pg->ref = 1;
		memset((void *)pg_to_addr(pg), 0, PAGE_SIZE);		//清空整页。
		pde[la >> 22].os = 0;
		pde[la >> 22].sign = 0x7;
		pde[la >> 22].pt_addr = (u32)pg >> 22;		//页目录表中添上刚刚申请的那个页！
	}
	return &((struct pte_t *)((pde[la >> 22].pt_addr << 12) + VERTUAL_MEM))[(la >> 12) & 0x3ff];
}

//通过pte得到页的地址la
u32 get_pg_addr_la(struct pte_t * pte)
{
	return (pte->page_addr << 12) + VERTUAL_MEM;
}

//通过pte得到页的地址pa
u32 get_pg_addr_pa(struct pte_t * pte)
{
	return (pte->page_addr) << 12;
}

void map(struct pde_t* pde, u32 la, u32 pa, u8 is_user)
{
	struct pte_t *pte = get_pte(pde, la, 1);
	struct Page *new_pg = addr_to_pg(la);
	new_pg->ref += 1;
	if((pte->sign & 0x1) == 1){		//要映射的pte已经被占用了 就要替换
		struct Page *pg = addr_to_pg(get_pg_addr_la(pte));
		if(pg == new_pg)	pg->ref -= 1;
		else 				unmap(pde, get_pg_addr_la(pte));
	}
	pte->sign = is_user == 1 ? 0x7 : 0x3;
	pte->page_addr = pa;
	asm volatile ("invlpg (%0)" ::"r"(la));
}

void unmap(struct pde_t *pde, u32 la)
{
	struct pte_t *pte = get_pte(pde, la, 0);
	if((pte->sign & 0x1) == 1){
		struct Page *pg = addr_to_pg(get_pg_addr_la(pte));
		pg->ref -= 1;
		if(pg->ref == 0){
			free_page(pg, 1);
		}
		pte->page_addr = 0;
		pte->sign = 0;
		asm volatile ("invlpg (%0)" ::"r"(la));
	}
}
