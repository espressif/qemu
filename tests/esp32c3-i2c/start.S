    .section .text.start, "ax"
    .global _start
_start:
    la sp, _stack_top
    j   test_main
