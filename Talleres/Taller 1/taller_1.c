#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

static sigjmp_buf salto_sigint;
static sigjmp_buf salto_sigusr1;
static sigjmp_buf salto_sigusr2;

void handler_sigint (int signo)  {
    printf("\nSe recibió la señal %d\n", signo);
    siglongjmp(salto_sigint, 1);
}

void handler_sigusr1 (int signo) {
    printf("\nSe recibió la señal %d\n", signo);
    siglongjmp(salto_sigusr1, 1);
}

void handler_sigusr2 (int signo) {
    printf("\nSe recibió la señal %d\n", signo);
    siglongjmp(salto_sigusr2, 1);
}

/*
void manejador(int signo)
{
    siglongjmp(punto_salto, 1);
}
*/

int main(void) {

    // Registrar los manejadores
    
    signal(SIGINT, handler_sigint);
    signal(SIGUSR1, handler_sigusr1);
    signal(SIGUSR2, handler_sigusr2);

    printf("PID del proceso: %d\n", getpid());

    if (sigsetjmp(salto_sigint, 1) != 0) {
        printf(">>> Recuperación en punto SIGINT\n");
        printf("Codigo ejecutandose desde sigint\n");
    }
    
    if (sigsetjmp(salto_sigusr1, 1) != 0) {
        printf(">>> Recuperación en punto SIGUSR1\n");
        printf("Codigo ejecutandose desde sigusr1\n");
    }
    
    if (sigsetjmp(salto_sigusr2, 1) != 0){
        printf(">>> Recuperación en punto SIGUSR2\n");
        printf("Codigo ejecutandose desde sigusr2\n");
    }
    
    while (1) {
        printf("Esperando señales...\n");
        sleep(2);
    }
}
