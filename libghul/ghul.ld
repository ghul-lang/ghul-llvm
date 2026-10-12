/*
 * Added to every link. LLVM writes the fault map of an object's folded
 * null checks to .llvm_faultmaps, which nothing refers to, so --gc-sections
 * would drop it. Kept, and bounded by two symbols, so libghul can find the
 * block a faulting read resumes at.
 */
SECTIONS
{
    .llvm_faultmaps : {
        __ghul_faultmaps_start = .;
        KEEP(*(.llvm_faultmaps))
        __ghul_faultmaps_end = .;
    }
}
INSERT AFTER .rodata;
