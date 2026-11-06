ENTRY(_start)
SECTIONS {
    . = 0x40390000;
    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) *(.sdata*) }
    .bss : { *(.bss*) *(.sbss*) *(COMMON) }
    . = ALIGN(16);
    . = . + 0x1000;
    _stack_top = .;
}
