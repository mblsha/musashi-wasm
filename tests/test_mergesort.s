* M68K Test Program with Merge Sort Implementation
* This program demonstrates merge sort with instruction tracing

    ORG     $400

* Main program
main:
    lea     array,a0        * Load array address
    move.w  #0,d0          * Start index = 0
    move.w  #7,d1          * End index = 7 (8 elements)
    bsr     mergesort      * Call mergesort(array, 0, 7)
    
    * Store sorted flag to indicate completion
    move.w  #$CAFE,sorted_flag
    
    stop    #$2000         * Stop execution

* Merge sort function
* Input: a0 = array pointer, d0.w = start, d1.w = end
* Uses: d2 = mid, d3-d7 for temp, a1-a3 for pointers
mergesort:
    movem.l d0-d7/a0-a3,-(sp)  * Save registers
    
    cmp.w   d0,d1              * if start >= end, return
    ble     .done
    
    * Calculate mid = (start + end) / 2
    move.w  d0,d2
    add.w   d1,d2
    lsr.w   #1,d2              * d2 = mid
    
    * Save current parameters
    move.w  d1,-(sp)           * Save end
    move.w  d2,-(sp)           * Save mid
    move.w  d0,-(sp)           * Save start
    
    * mergesort(arr, start, mid)
    move.w  d2,d1              * end = mid
    bsr     mergesort
    
    * Restore and prepare for second call
    move.w  (sp)+,d0           * Restore start
    move.w  (sp)+,d2           * Restore mid
    move.w  (sp)+,d1           * Restore end
    
    * Save parameters again
    move.w  d1,-(sp)           * Save end
    move.w  d2,-(sp)           * Save mid  
    move.w  d0,-(sp)           * Save start
    
    * mergesort(arr, mid+1, end)
    move.w  d2,d0
    addq.w  #1,d0              * start = mid + 1
    bsr     mergesort
    
    * Restore for merge
    move.w  (sp)+,d0           * start
    move.w  (sp)+,d2           * mid
    move.w  (sp)+,d1           * end
    
    * merge(arr, start, mid, end)
    bsr     merge
    
.done:
    movem.l (sp)+,d0-d7/a0-a3  * Restore registers
    rts

* Merge function - merges two sorted subarrays
* Input: a0 = array, d0 = start, d2 = mid, d1 = end
* Uses workspace for temporary storage
merge:
    movem.l d0-d7/a0-a3,-(sp)  * Save registers
    
    * Copy left half to workspace
    move.w  d0,d3              * i = start
    lea     workspace,a1       * a1 = workspace pointer
.copy_left:
    cmp.w   d2,d3              * while i <= mid
    bgt     .copy_right_init
    move.w  d3,d4
    lsl.w   #1,d4              * d4 = i * 2 (word offset)
    move.w  0(a0,d4.w),(a1)+   * workspace[k++] = arr[i]
    addq.w  #1,d3              * i++
    bra     .copy_left
    
.copy_right_init:
    move.w  d2,d3
    addq.w  #1,d3              * i = mid + 1
.copy_right:
    cmp.w   d1,d3              * while i <= end
    bgt     .merge_back
    move.w  d3,d4
    lsl.w   #1,d4              * d4 = i * 2
    move.w  0(a0,d4.w),(a1)+   * workspace[k++] = arr[i]
    addq.w  #1,d3              * i++
    bra     .copy_right
    
.merge_back:
    * Merge from workspace back to array
    lea     workspace,a1       * Left pointer
    move.w  d2,d3
    sub.w   d0,d3
    addq.w  #1,d3              * d3 = left_size = mid - start + 1
    
    lea     workspace,a2
    move.w  d3,d4
    lsl.w   #1,d4
    add.w   d4,a2              * a2 = Right pointer
    
    move.w  d1,d4
    sub.w   d2,d4              * d4 = right_size = end - mid
    
    move.w  d0,d5              * d5 = current index in array
    
.merge_loop:
    * Check if we're done with left
    tst.w   d3
    beq     .copy_remaining_right
    
    * Check if we're done with right
    tst.w   d4
    beq     .copy_remaining_left
    
    * Compare and copy smaller element
    move.w  (a1),d6            * Left element
    move.w  (a2),d7            * Right element
    cmp.w   d7,d6
    bgt     .take_right
    
.take_left:
    move.w  d5,d7
    lsl.w   #1,d7
    move.w  d6,0(a0,d7.w)      * arr[d5] = left element
    addq.l  #2,a1              * Advance left pointer
    subq.w  #1,d3              * Decrement left count
    bra     .next_merge
    
.take_right:
    move.w  d5,d6
    lsl.w   #1,d6
    move.w  d7,0(a0,d6.w)      * arr[d5] = right element
    addq.l  #2,a2              * Advance right pointer
    subq.w  #1,d4              * Decrement right count
    
.next_merge:
    addq.w  #1,d5              * Increment array index
    cmp.w   d1,d5              * Check if done
    ble     .merge_loop
    bra     .merge_done
    
.copy_remaining_left:
    tst.w   d3
    beq     .merge_done
    move.w  d5,d6
    lsl.w   #1,d6
    move.w  (a1)+,0(a0,d6.w)
    addq.w  #1,d5
    subq.w  #1,d3
    bra     .copy_remaining_left
    
.copy_remaining_right:
    tst.w   d4
    beq     .merge_done
    move.w  d5,d6
    lsl.w   #1,d6
    move.w  (a2)+,0(a0,d6.w)
    addq.w  #1,d5
    subq.w  #1,d4
    bra     .copy_remaining_right
    
.merge_done:
    movem.l (sp)+,d0-d7/a0-a3  * Restore registers
    rts

* Data section
    EVEN
array:       dc.w    8,3,7,1,5,2,6,4     * Test array to sort
sorted_flag: dc.w    0                   * Set to $CAFE when done
workspace:   ds.w    16                  * Workspace for merge operations

    END