* M68K Test Program for Perfetto Trace Generation
* This program demonstrates function calls, nested calls, loops and memory operations

    ORG     $400

* Main program
main:
    move.w  #5,d0           * Set up parameter for factorial
    bsr     factorial       * Call factorial(5)
    move.l  d0,result1      * Store result
    
    move.w  #3,d0           * Set up parameter for fibonacci  
    bsr     fibonacci       * Call fibonacci(3)
    move.l  d0,result2      * Store result
    
    lea     array,a0        * Load array address
    move.w  #8,d0          * Array size
    bsr     bubble_sort     * Sort the array
    
    stop    #$2000         * Stop execution

* Factorial function - calculates n!
* Input: d0.w = n
* Output: d0.l = n!
factorial:
    cmp.w   #1,d0          * Check if n <= 1
    ble     .done          * If so, return 1
    
    move.w  d0,-(sp)       * Save n on stack
    subq.w  #1,d0          * n-1
    bsr     factorial      * Recursive call factorial(n-1)
    
    move.w  (sp)+,d1       * Restore n from stack
    mulu    d1,d0          * n * factorial(n-1)
.done:
    rts

* Fibonacci function - calculates nth fibonacci number  
* Input: d0.w = n
* Output: d0.l = fib(n)
fibonacci:
    cmp.w   #2,d0          * Check if n < 2
    blt     .base_case     * If so, return n
    
    move.w  d0,-(sp)       * Save n
    subq.w  #1,d0          * n-1
    bsr     fibonacci      * fib(n-1)
    move.l  d0,-(sp)       * Save result
    
    move.w  4(sp),d0       * Get n back
    subq.w  #2,d0          * n-2  
    bsr     fibonacci      * fib(n-2)
    
    add.l   (sp)+,d0       * fib(n-1) + fib(n-2)
    addq.l  #2,sp          * Clean up stack
    rts
    
.base_case:
    ext.l   d0             * Extend to long
    rts

* Bubble sort function
* Input: a0 = array pointer, d0.w = size
* Modifies array in place
bubble_sort:
    movem.l d0-d3/a0-a1,-(sp)  * Save registers
    
    subq.w  #1,d0              * size-1 for outer loop
    move.w  d0,d2              * d2 = outer counter
    
.outer_loop:
    tst.w   d2                 * Check if done
    beq     .done              * Exit if zero
    
    move.w  d2,d3              * d3 = inner counter
    move.l  a0,a1              * Reset array pointer
    
.inner_loop:
    move.w  (a1),d0            * Get current element
    move.w  2(a1),d1           * Get next element
    cmp.w   d1,d0              * Compare
    ble     .no_swap           * Skip if in order
    
    move.w  d1,(a1)            * Swap elements
    move.w  d0,2(a1)
    
.no_swap:
    addq.l  #2,a1              * Move to next element
    subq.w  #1,d3              * Decrement inner counter
    bne     .inner_loop        * Continue inner loop
    
    subq.w  #1,d2              * Decrement outer counter
    bra     .outer_loop        * Continue outer loop
    
.done:
    movem.l (sp)+,d0-d3/a0-a1  * Restore registers
    rts

* Data section
    EVEN
array:      dc.w    8,3,7,1,5,2,6,4     * Test array
result1:    dc.l    0                   * Factorial result
result2:    dc.l    0                   * Fibonacci result

    END