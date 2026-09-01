#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <stdlib.h>

#define MAXSIZE 128

struct msgbuf {
    long mtype;
    char mtext[MAXSIZE];
};

static sigjmp_buf salto_sigint;
static sigjmp_buf salto_sigusr1;
static sigjmp_buf salto_sigusr2;

void die(char *s) {
    perror(s);
    exit(1);
}

void handler_sigint(int signo) {
    (void)signo;
    siglongjmp(salto_sigint, 1);
}

void handler_sigusr1(int signo) {
    (void)signo;
    siglongjmp(salto_sigusr1, 1);
}

void handler_sigusr2(int signo) {
    (void)signo;
    siglongjmp(salto_sigusr2, 1);
}

void enviar_mensaje(int msqid, long tipo, const char *texto) {
    struct msgbuf sbuf;
    size_t buflen;

    sbuf.mtype = tipo;
    strncpy(sbuf.mtext, texto, MAXSIZE - 1);
    sbuf.mtext[MAXSIZE - 1] = '\0';
    buflen = strlen(sbuf.mtext) + 1;

    if (msgsnd(msqid, &sbuf, buflen, IPC_NOWAIT) < 0)
        die("msgsnd");

    printf(">>> Mensaje tipo %ld enviado a la cola IPC: \"%s\"\n", tipo, texto);
}

int main(void) {
    int msqid;
    int msgflg = IPC_CREAT | 0666;
    key_t key = 1234;

    if ((msqid = msgget(key, msgflg)) < 0)
        die("msgget");

    signal(SIGINT, handler_sigint);
    signal(SIGUSR1, handler_sigusr1);
    signal(SIGUSR2, handler_sigusr2);

    printf("PID del proceso: %d\n\n", getpid());

    if (sigsetjmp(salto_sigint, 1) != 0) {
        printf("\nSIGINT recibida\n");
        enviar_mensaje(msqid, 1, "Senal tipo 1");
    }
    if (sigsetjmp(salto_sigusr1, 1) != 0) {
        printf("\nSIGUSR1 recibida\n");
        enviar_mensaje(msqid, 2, "Senal tipo 2");
    }
    if (sigsetjmp(salto_sigusr2, 1) != 0) {
        printf("\nSIGUSR2 recibida\n");
        enviar_mensaje(msqid, 3, "Senal tipo 3");
    }

    while (1) {
        printf("Esperando se...\n");
        sleep(2);
    }

    return 0;
}
