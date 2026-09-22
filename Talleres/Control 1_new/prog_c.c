/*
 * proceso_c.c  (version con dup2)
 *
 * Ya no recibe el fd del pipe por argv: el Clon de B lo redirigio
 * a STDIN_FILENO con dup2 antes del execv, asi que aqui simplemente
 * leemos de la entrada estandar con fgets/read normal.
 *
 * Compilar: gcc -o proceso_c proceso_c.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_TEXT 256

int main(void) {
    char buffer[MAX_TEXT];

    printf("[Proceso C, PID %d] Transformado via execv. Leyendo de STDIN (dup2 del pipe)...\n",
           getpid());

    while (1) {
        ssize_t n = read(STDIN_FILENO, buffer, MAX_TEXT);
        if (n < 0) {
            perror("[Proceso C] read");
            exit(1);
        }
        if (n == 0) {
            printf("[Proceso C] El otro extremo del pipe se cerro.\n");
            break;
        }
        printf("[Proceso C, PID %d] Mensaje recibido (via dup2/STDIN): %s\n", getpid(), buffer);
    }

    return 0;
}
