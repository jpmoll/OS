/*
 * proceso_a.c  (version extendida con FIFO y dup2)
 *
 * Proceso A (padre principal):
 *  - Crea el pipe SIN NOMBRE y la cola de mensajes (System V) ANTES del fork.
 *  - Crea ademas un FIFO NOMBRADO en /tmp/fifo_practica (mkfifo).
 *  - Captura 3 senales con el patron sigsetjmp/siglongjmp:
 *      SIGINT  (2)  -> envia mensaje a B por la cola de mensajes (msgsnd)
 *      SIGTRAP (5)  -> envia mensaje a C por el pipe sin nombre (write)
 *      SIGUSR1 (10) -> envia mensaje a D por el FIFO nombrado (write)
 *  - Crea a Proceso B (fork). B se clona; el clon usa dup2 + execv -> proceso_c.
 *  - Crea a Proceso D (fork simple, sin exec) que escucha el FIFO.
 *
 * Compilar:  gcc -o proceso_a proceso_a.c
 * Ejecutar:  ./proceso_a
 * Probar:    kill -2  <PID>   (SIGINT  -> B, cola de mensajes)
 *            kill -5  <PID>   (SIGTRAP -> C, pipe + dup2 + execv)
 *            kill -10 <PID>   (SIGUSR1 -> D, FIFO nombrado)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>

#define MAX_TEXT 256
#define FIFO_PATH "/tmp/fifo_practica"

struct msgbuf {
    long mtype;
    char mtext[MAX_TEXT];
};

static sigjmp_buf salto_sigint;
static sigjmp_buf salto_sigtrap;
static sigjmp_buf salto_sigusr1;

static int pipe_fd[2];
static int msgid;

void handler_sigint(int signo) {
    printf("\n[Proceso A] Senal %d (SIGINT) recibida\n", signo);
    siglongjmp(salto_sigint, 1);
}

void handler_sigtrap(int signo) {
    printf("\n[Proceso A] Senal %d (SIGTRAP) recibida\n", signo);
    siglongjmp(salto_sigtrap, 1);
}

void handler_sigusr1(int signo) {
    printf("\n[Proceso A] Senal %d (SIGUSR1) recibida\n", signo);
    siglongjmp(salto_sigusr1, 1);
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

void enviar_a_d(void) {
    /* El FIFO se abre solo para esta escritura puntual.
       open() bloquea hasta que D tenga el otro extremo abierto en lectura. */
    int fd_fifo = open(FIFO_PATH, O_WRONLY);
    if (fd_fifo == -1) {
        perror("[Proceso A] open FIFO");
        return;
    }

    char mensaje[MAX_TEXT];
    snprintf(mensaje, MAX_TEXT, "Mensaje de A hacia D (PID emisor: %d)", getpid());

    if (write(fd_fifo, mensaje, strlen(mensaje) + 1) == -1) {
        perror("[Proceso A] write FIFO");
    } else {
        printf("[Proceso A] Mensaje enviado a D via FIFO.\n");
    }
    close(fd_fifo);
}

/* Codigo que ejecuta el Proceso B (rama hija del primer fork) */
void proceso_b(void) {
    pid_t clon_pid = fork();

    if (clon_pid < 0) {
        perror("[Proceso B] fork (clon)");
        exit(1);
    }

    if (clon_pid == 0) {
        /* Clon de B: se transforma en Proceso C via execv,
           usando dup2 para redirigir el extremo de lectura del pipe a STDIN */
        close(pipe_fd[1]); /* el clon no escribe */

        if (dup2(pipe_fd[0], STDIN_FILENO) == -1) {
            perror("[Clon de B] dup2");
            exit(1);
        }
        close(pipe_fd[0]); /* ya duplicado en STDIN, cerramos el original */

        char *args[] = { "./proceso_c", NULL };

        printf("[Clon de B, PID %d] dup2 aplicado, ejecutando execv hacia proceso_c...\n", getpid());
        execv("./proceso_c", args);

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

/* Codigo que ejecuta el Proceso D (escucha el FIFO nombrado) */
void proceso_d(void) {
    printf("[Proceso D, PID %d] Escuchando FIFO %s...\n", getpid(), FIFO_PATH);

    char buffer[MAX_TEXT];
    while (1) {
        int fd_fifo = open(FIFO_PATH, O_RDONLY); /* bloquea hasta que A escriba */
        if (fd_fifo == -1) {
            perror("[Proceso D] open FIFO");
            exit(1);
        }

        ssize_t n = read(fd_fifo, buffer, MAX_TEXT);
        if (n > 0) {
            printf("[Proceso D, PID %d] Mensaje recibido por FIFO: %s\n", getpid(), buffer);
        }
        close(fd_fifo);
    }
}

int main(void) {
    /* 1. Pipe sin nombre */
    if (pipe(pipe_fd) == -1) {
        perror("[Proceso A] pipe");
        exit(1);
    }

    /* 2. Cola de mensajes System V */
    msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("[Proceso A] msgget");
        exit(1);
    }

    /* 3. FIFO nombrado */
    unlink(FIFO_PATH); /* por si quedo de una corrida anterior */
    if (mkfifo(FIFO_PATH, 0666) == -1) {
        perror("[Proceso A] mkfifo");
        exit(1);
    }

    /* 4. Manejadores de senales */
    signal(SIGINT, handler_sigint);
    signal(SIGTRAP, handler_sigtrap);
    signal(SIGUSR1, handler_sigusr1);

    printf("[Proceso A] PID: %d\n", getpid());
    printf("[Proceso A] kill -2  %d  -> SIGINT  (mensaje a B, cola)\n", getpid());
    printf("[Proceso A] kill -5  %d  -> SIGTRAP (mensaje a C, pipe+dup2)\n", getpid());
    printf("[Proceso A] kill -10 %d  -> SIGUSR1 (mensaje a D, FIFO)\n", getpid());

    /* 5. Crear a D primero, para que ya este escuchando el FIFO */
    pid_t d_pid = fork();
    if (d_pid < 0) {
        perror("[Proceso A] fork (D)");
        exit(1);
    }
    if (d_pid == 0) {
        proceso_d();
        exit(0);
    }

    /* 6. Crear a B (que a su vez crea al clon -> C) */
    pid_t b_pid = fork();
    if (b_pid < 0) {
        perror("[Proceso A] fork (B)");
        exit(1);
    }
    if (b_pid == 0) {
        proceso_b();
        exit(0);
    }

    /* Proceso A no necesita el extremo de lectura del pipe */
    close(pipe_fd[0]);

    /* Puntos de recuperacion para cada senal */
    if (sigsetjmp(salto_sigint, 1) != 0) {
        enviar_a_b();
    }
    if (sigsetjmp(salto_sigtrap, 1) != 0) {
        enviar_a_c();
    }
    if (sigsetjmp(salto_sigusr1, 1) != 0) {
        enviar_a_d();
    }

    while (1) {
        pause();
    }

    return 0;
}
