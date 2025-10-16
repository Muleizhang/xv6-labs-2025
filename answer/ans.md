# Inspect a user-process page table (easy)

Run make qemu and run the user program pgtbltest. The print_pgtbl functions prints out the page-table entries for the first 10 and last 10 pages of the pgtbltest process using the pgpte system call that we added to xv6 for this lab. The output looks as follows:


```sh
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ pgtbltest
print_pgtbl starting
va 0x0 pte 0x21FC885B pa 0x87F22000 perm 0x5B
va 0x1000 pte 0x21FC7C5B pa 0x87F1F000 perm 0x5B
va 0x2000 pte 0x21FC7817 pa 0x87F1E000 perm 0x17
va 0x3000 pte 0x21FC7407 pa 0x87F1D000 perm 0x7
va 0x4000 pte 0x21FC70D7 pa 0x87F1C000 perm 0xD7
va 0x5000 pte 0x0 pa 0x0 perm 0x0
va 0x6000 pte 0x0 pa 0x0 perm 0x0
va 0x7000 pte 0x0 pa 0x0 perm 0x0
va 0x8000 pte 0x0 pa 0x0 perm 0x0
va 0x9000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFF6000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFF7000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFF8000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFF9000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFFA000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFFB000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFFC000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFFD000 pte 0x0 pa 0x0 perm 0x0
va 0x3FFFFFE000 pte 0x21FD08C7 pa 0x87F42000 perm 0xC7
va 0x3FFFFFF000 pte 0x2000184B pa 0x80006000 perm 0x4B
print_pgtbl: OK
ugetpid_test starting
usertrap(): unexpected scause 0xd pid=3
            sepc=0x7d0 stval=0x3fffffd000
$
```

### Q1 For every page table entry in the print_pgtbl output, explain what it logically contains and what its permission bits are. Figure 3.4 in the xv6 book might be helpful, although note that the figure might have a slightly different set of pages than process that's being inspected here. Note that xv6 doesn't place the virtual pages consecutively in physical memory.

对于 print_pgtbl 输出中的每一个页表项 (Page Table Entry, PTE)，解释它逻辑上包含什么内容以及它的权限位 (permission bits) 是什么。xv6 书中的图 3.4 可能会有帮助，不过请注意，该图的页集合可能与此处检查的进程略有不同。另外，请注意 xv6 不会将虚拟页连续地放置在物理内存中。

![alt text](image.png)


### 左侧：进程的用户虚拟地址空间布局

这个图从虚拟地址$0$开始到$MAXVA$（最大虚拟地址）描述了一个进程的内存段布局。

1.  **text 段（代码段）**
    * **位置:** 接近虚拟地址$0$的底部。
    * **内容:** 存放程序的可执行机器指令。
    * **权限 ($R-XU$):**
        * $R$: Read (可读)
        * $-X$: Execute (可执行)
        * $U$: User (用户模式下可访问)
        * **特点:** 不可写，保证代码不会被意外修改。

2.  **data 段 (数据段) 和 unused (未用/保留)**
    * **位置:** 在 `text` 段之上，紧接着一个 `unused` 区域。
    * **内容:** 存放已初始化的全局变量和静态变量。
    * **权限 ($R-WU$):**
        * $R$: Read (可读)
        * $W$: Write (可写)
        * $U$: User (用户模式下可访问)
        * **特点:** 可读写，用于存储程序运行时可能修改的数据。
    * **Page aligned:** 指出这片区域的起始地址通常是页对齐的。

3.  **guard page (保护页)**
    * **位置:** 在 `data` 段上方，紧邻 `stack` 段下方。
    * **内容:** 这是一个特殊的、通常**不可访问**的页。
    * **目的:** 用于检测栈溢出。如果栈向下增长（很多架构如x86是这样），当栈指针试图访问这页时，会触发一个页错误（或保护错误），操作系统可以捕获这个错误，从而防止栈和下面的 `data` 段或其它重要区域重叠造成内存破坏。

4.  **stack 段 (栈)**
    * **位置:** 紧靠 `guard page` 上方，通常从一个相对较高的地址向下增长。
    * **内容:** 存放函数调用时的局部变量、函数参数、返回地址等。
    * **权限 ($R-WU$):**
        * $R$: Read (可读)
        * $W$: Write (可写)
        * $U$: User (用户模式下可访问)
    * **特点:** 栈的大小是动态变化的，但通常不会超过其分配的初始大小（图中显示它与 `heap` 段之间有一个 `PAGESIZE` 的分隔，可能是 guard page）。

5.  **heap 段 (堆)**
    * **位置:** 在 `stack` 段上方，与 `stack` 段通常在中间区域相向增长（`stack` 向下，`heap` 向上）。
    * **内容:** 用于动态内存分配（例如 `malloc` 或 `new` 分配的内存）。
    * **权限 ($R-WU$):** 可读、可写、用户模式可访问。
    * **特点:** 运行时根据需要动态扩展。

6.  **unused (未用/保留)**
    * **位置:** 在 `heap` 段和顶部之间。
    * **内容:** 保留区域。

7.  **trapframe 和 trampoline**
    * **位置:** 靠近 `MAXVA` 的最顶部。
    * **trapframe:**
        * **内容:** 用于在内核和用户态之间切换时保存/恢复进程的 CPU 寄存器状态的结构。
        * **权限 ($R-WU$):** 可读、可写、用户模式可访问。
    * **trampoline:**
        * **内容:** 一小段机器代码，用于处理系统调用、中断和异常，负责用户态和内核态的过渡。
        * **权限 ($RX--$):**
            * $R$: Read (可读)
            * $X$: Execute (可执行)
            * $--$: 通常表示**内核独占**或特殊权限。在xv6的RISC-V架构中，这块区域的虚拟地址是用户地址空间的一部分，但它映射到的物理页实际上包含了内核的 `trampoline` 代码，且该代码被用于进入内核。
        * **特点:** 尽管在用户地址空间内，但其映射和访问受内核严格控制。

### 右侧：初始栈内容（Initial Stack）

右侧的框图详细展示了进程启动时，操作系统在初始的 `stack` 段中设置的内容，主要是为了调用 `main` 函数做准备（传递 `argc` 和 `argv`）。

从栈顶（高地址）向栈底（低地址）看，内容通常包括：

1.  **argument 0 到 argument N (环境字符串/辅助向量)**
    * **内容:** 可能包括环境变量字符串以及内核传递给进程的辅助向量（auxiliary vector）信息。
2.  **nul-terminated string 0**
    * 这是第一个参数字符串（例如程序名）的空终止符（`\0`）。
3.  **...**
    * 其他参数字符串的空终止符。
4.  **address of argument N**
    * 这是指向第 $N$ 个参数字符串（或环境字符串）在栈上实际存储位置的地址。
    * **`argv[argc]`:** 这个位置通常是 `NULL` (地址 $0$)，标记 `argv` 数组的结束。
5.  **...**
    * 其他参数字符串地址。
6.  **address of argument 0**
    * 指向第 $0$ 个参数字符串（即程序名）在栈上存储位置的地址。
    * **`argv[0]`:** 这是 `argv` 数组的第一个元素，指向程序名字符串。
7.  **address of address of argument 0**
    * **`argv` argument of main:** 这是指向 `argv` 数组起始位置的地址，即 `main` 函数需要的 `char *argv[]` 参数。
    * 在它上面通常还有一个值，即 **`argc`**（参数计数，未明确画出，但通常位于最前面，有时在 `argv` 数组地址的上方）。

8.  **(empty) (空闲区域)**
    * 这是栈中当前未使用的区域。进程运行时，函数调用会从这里开始使用空间。


### RISC-V PTE 权限位（Perm Bits）速查表

| Bit | Hex Value | Name | Meaning |
| :---: | :---: | :---: | :--- |
| 0 | $0\text{x}1$ | V | **Valid (有效)** |
| 1 | $0\text{x}2$ | R | **Read (可读)** |
| 2 | $0\text{x}4$ | W | **Write (可写)** |
| 3 | $0\text{x}8$ | X | **Execute (可执行)** |
| 4 | $0\text{x}10$ | U | **User (用户模式可访问)** |
| 5 | $0\text{x}20$ | G | Global |
| 6 | $0\text{x}40$ | A | Accessed |
| 7 | $0\text{x}80$ | D | Dirty/Modified |

### 用户进程页表项 (PTE) 总结分析

| `va` | `perm` (Hex) | `perm` (Bits) | 权限组合 | 逻辑内容（参考 Figure 3.4）| 备注 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **0x0** | $0\text{x}5\text{B}$ | V, R, X, U, A, G | **R-XU** | **Text Segment (代码段)** | 进程的可执行代码。 |
| **0x1000** | $0\text{x}5\text{B}$ | V, R, X, U, A, G | **R-XU** | **Text Segment (代码段)** | 进程的可执行代码。 |
| **0x2000** | $0\text{x}17$ | V, R, W, U | **RWU** | **Data/BSS Segment (数据段)** | 进程的全局变量和静态数据，可读写。 |
| **0x3000** | $0\text{x}7$ | V, R, W | **RW** | **Data/BSS Segment 或特殊内核页** | **缺少 U 位！** 这意味着只有内核模式才能访问。它可能是 Data/BSS 段的一个特殊部分，被内核用作只读写但不希望用户直接访问的页。 |
| **0x4000** | $0\text{xD}7$ | V, R, W, U, A, D | **RWU** | **Stack/Data Segment (栈或数据段)** | 具有完整的读写权限，且可被用户访问。 |
| **0x5000** to **0x9000** | $0\text{x}0$ | None (V=0) | **无效** | **Unused / Gap** | 进程未分配的虚拟地址或 Guard Page (保护页)。 |
| **... (中间区域)** | $0\text{x}0$ | None (V=0) | **无效** | **Unused / Gap** | 堆和栈之间的巨大未分配区域。 |
| **0x3FFFFFE000** | $0\text{xC}7$ | V, R, W, U, A, D | **RWU** | **Trapframe** | 用于保存/恢复 CPU 寄存器状态的结构，内核和用户模式都需要访问。 |
| **0x3FFFFFF000** | $0\text{x}4\text{B}$ | V, R, X, A, G | **R-XU** | **Trampoline** | 内核用于处理系统调用和中断的跳板代码。它是用户地址空间中最高的一页。 |

### 关于 `va 0x3000` 的重新确认

对于 `va 0x3000` (perm $0\text{x}7$):

| `va` | `perm` | 权限 | 访问者 | 解释 |
| :--- | :--- | :--- | :--- | :--- |
| **0x3000** | $0\text{x}7$ | $V, R, W$ | **仅限内核** | 尽管该地址在用户进程的低地址空间（本应是 Data/BSS），但 **缺少 U 位** 意味着用户程序无法合法访问它。它可能是一个特殊的数据结构，被内核放在这个位置以供其内部使用。|


# Speed up system calls (easy)
