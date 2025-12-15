/*
 * Dummy exit() to satisfy libnix linkage when -nostartfiles is used.
 * We're a shared library/MUI class, we should never call exit().
 */
void exit(int status)
{
    (void)status;
    while(1);
}
