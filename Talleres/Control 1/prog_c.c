/*
 * proceso_c.c
 *
 * Proceso C: ejecutable independiente.
 * El Clon de B llega aqui via execv("./proceso_c", argv), pasando
 * el descriptor de lectura del pipe como argv[1] (string).
 *
 * Compilar: gcc -o proceso_c proceso_c.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_TEXT 256

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <fd_lectura_pipe>\n", argv[0]);
        exit(1);
    }

    int fd_lectura = atoi(argv[1]);
    char buffer[MAX_TEXT];

    printf("[Proceso C, PID %d] Transformado via execv. Escuchando pipe (fd %d)...\n",
           getpid(), fd_lectura);

    while (1) {
        ssize_t n = read(fd_lectura, buffer, MAX_TEXT);
        if (n < 0) {
            perror("[Proceso C] read");
            exit(1);
        }
        if (n == 0) {
            printf("[Proceso C] El otro extremo del pipe se cerro.\n");
            break;
        }
        printf("[Proceso C, PID %d] Mensaje recibido por pipe: %s\n", getpid(), buffer);
    }

    return 0;
}
