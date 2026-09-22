/*
 * proceso_a.c
 *
 * Proceso A (padre principal):
 *  - Crea el pipe y la cola de mensajes (System V) ANTES de hacer fork,
 *    para que B los herede automaticamente.
 *  - Captura SIGINT (2) y SIGTRAP (5) usando el patron sigsetjmp/siglongjmp.
 *  - Ante SIGINT -> envia mensaje a B por la cola (msgsnd).
 *  - Ante SIGTRAP -> envia mensaje a C por el pipe (write).
 *  - Crea a Proceso B (fork). B se clona y el clon hace execv a proceso_c.
 *
 * Compilar:  gcc -o proceso_a proceso_a.c
 * (System V IPC no necesita -lrt)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_TEXT 256

struct msgbuf {
    long mtype;
    char mtext[MAX_TEXT];
};

static sigjmp_buf salto_sigint;
static sigjmp_buf salto_sigtrap;

static int pipe_fd[2];
static int msgid;

void handler_sigint(int signo) {
    printf("\n[Proceso A] Se recibio la señal %d (SIGINT)\n", signo);
    siglongjmp(salto_sigint, 1);
}

void handler_sigtrap(int signo) {
    printf("\n[Proceso A] Se recibio la señal %d (SIGTRAP)\n", signo);
    siglongjmp(salto_sigtrap, 1);
}

void enviar_a_b(void) {
    struct msgbuf msg;
    msg.mtype = 1;
    snprintf(msg.mtext, MAX_TEXT, "Mensaje de A hacia B (PID emisor: %d)", getpid());

    if (msgsnd(msgid, &msg, strlen(msg.mtext) + 1, 0) == -1) {
        perror("[Proceso A] msgsnd");
        return;
    }
    printf("[Proceso A] Mensaje enviado a B via cola de mensajes.\n");
}

void enviar_a_c(void) {
    char mensaje[MAX_TEXT];
    snprintf(mensaje, MAX_TEXT, "Mensaje de A hacia C (PID emisor: %d)", getpid());

    if (write(pipe_fd[1], mensaje, strlen(mensaje) + 1) == -1) {
        perror("[Proceso A] write pipe");
        return;
    }
    printf("[Proceso A] Mensaje enviado a C via pipe.\n");
}

/* Codigo que ejecuta el Proceso B (rama hija del primer fork) */
void proceso_b(void) {
    pid_t clon_pid = fork();

    if (clon_pid < 0) {
        perror("[Proceso B] fork (clon)");
        exit(1);
    }

    if (clon_pid == 0) {
        /* Clon de B: se transforma en Proceso C via execv */
        close(pipe_fd[1]); /* el clon no escribe, solo pasa el fd de lectura */

        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", pipe_fd[0]);

        char *args[] = { "./proceso_c", fd_str, NULL };

        printf("[Clon de B, PID %d] Ejecutando execv hacia proceso_c...\n", getpid());
        execv("./proceso_c", args);

        /* Solo se llega aqui si execv falla */
        perror("[Clon de B] execv");
        exit(1);
    }

    /* Proceso B original: no necesita el pipe */
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    printf("[Proceso B, PID %d] Esperando mensajes en la cola (msgrcv)...\n", getpid());

    struct msgbuf msg;
    while (1) {
        if (msgrcv(msgid, &msg, MAX_TEXT, 1, 0) == -1) {
            perror("[Proceso B] msgrcv");
            exit(1);
        }
        printf("[Proceso B, PID %d] Mensaje recibido: %s\n", getpid(), msg.mtext);
    }
}

int main(void) {
    /* 1. Crear el pipe (sin nombre) - se hereda por fork */
    if (pipe(pipe_fd) == -1) {
        perror("[Proceso A] pipe");
        exit(1);
    }

    /* 2. Crear la cola de mensajes (System V) */
    msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("[Proceso A] msgget");
        exit(1);
    }

    /* 3. Registrar manejadores de señales */
    signal(SIGINT, handler_sigint);
    signal(SIGTRAP, handler_sigtrap);

    printf("[Proceso A] PID: %d\n", getpid());
    printf("[Proceso A] Ejecuta 'kill -2 %d'  -> SIGINT  (mensaje a B)\n", getpid());
    printf("[Proceso A] Ejecuta 'kill -5 %d'  -> SIGTRAP (mensaje a C)\n", getpid());

    /* 4. Crear a B (que a su vez crea al clon -> C) */
    pid_t b_pid = fork();
    if (b_pid < 0) {
        perror("[Proceso A] fork (B)");
        exit(1);
    }

    if (b_pid == 0) {
        proceso_b();
        exit(0); /* no deberia llegar aqui */
    }

    /* Proceso A no necesita el extremo de lectura del pipe */
    close(pipe_fd[0]);

    /* Puntos de recuperacion para cada señal (mismo patron que ya tenias) */
    if (sigsetjmp(salto_sigint, 1) != 0) {
        enviar_a_b();
    }

    if (sigsetjmp(salto_sigtrap, 1) != 0) {
        enviar_a_c();
    }

    /* 5. Espera pasiva de señales */
    while (1) {
        pause();
    }

    return 0;
}
