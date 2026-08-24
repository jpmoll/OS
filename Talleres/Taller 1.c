#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

static sigjmp_buf punto_salto;

void manejador(int signo) {
    printf("\nSe recibió la señal %d\n", signo);
    siglongjmp(punto_salto, signo);
}

int main(void) {
    
    signal(SIGINT, manejador);   // SIGINT -2 (Ctrl+C)
    
    signal(SIGQUIT, manejador);  // SIGQUIT -3
    signal(SIGILL, manejador);   // SIGILL -4
    signal(SIGTRAP, manejador);  // SIGTRAP -5
    
    int respuesta = sigsetjmp(punto_salto, 1);
    
    if (respuesta == 0) {
        printf("Programa iniciado.\n");
        while (1) {
            printf("Ejecutando código normal...\n");
            sleep(2);
        }
    }
    
    else if (respuesta == 2){  // SIGINT 2 Terminal interrupt (ANSI)
        printf("Salto de código por SIGINT(2)\n");
        while (1) {
            printf("Ejecutando rama 2...\n");
            sleep(2);
        }
    }
    
    else if (respuesta == 3){  // SIGQUIT 3 Terminal quit (POSIX)
        printf("Salto de código por SIGQUIT(3)\n");
        while (1) {
            printf("Ejecutando rama 3...\n");
            sleep(2);
        }
    }
    
    else if (respuesta == 4){  // SIGILL 4 Illegal instruction (ANSI)
        printf("Salto de código por SIGILL(4)\n");
        while (1) {
            printf("Ejecutando rama 4...\n");
            sleep(2);
        }
    }
    
    else if (respuesta == 5){  // SIGTRAP 5 Trace trap (POSIX)
        printf("Salto de código por SIGTRAP(5)\n");
        while (1) {
            printf("Ejecutando rama 5...\n");
            sleep(2);
        }
    }
    
    else {
        printf("Se realizó un salto\n");
        printf("Continuando desde el punto de recuperación...\n");
    }
    
    printf("Fin del programa.\n");
    
    return 0;
}
