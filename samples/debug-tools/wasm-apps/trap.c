__attribute__((always_inline)) static inline int
trap_helper(int n)
{
    /* Forced-inline helper to demonstrate inline call stack expansion.
       The trap is inside this helper so the runtime offset falls within
       the inlined region — addr2line.py will then show both trap_helper
       and c() in the call chain (innermost first). */
    __builtin_trap();
    return n + 100;
}

int
c(int n)
{
    return trap_helper(n);
}

int
b(int n)
{
    n += 3;
    return c(n);
}

int
a(int n)
{
    return b(n);
}

int
main(int argc, char **argv)
{
    int i = 5;
    a(i);

    return 0;
}
