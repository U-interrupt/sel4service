#include <stdio.h>

#include <sel4runtime.h>

/* When the root task exists, it should simply suspend itself */
static void root_exit(int code)
{
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);
}

int main(void) {
    /* Set exit handler */
    sel4runtime_set_exit(root_exit);

    printf("\n");
    printf("seL4 root server\n");
    printf("================\n");
    printf("\n");

    return 0;
}