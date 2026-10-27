/*
 * shm.c - Memoria compartida + semaforos con varios estados.
 *
 * Une lo de shm_server.c / shm_client.c (memoria compartida)
 * con semaphore.c (semaforos System V), reemplazando el
 * "while (*shm != '*') sleep(1);" por semaforos de verdad.
 *
 * Uso:
 *   ./shm c   -> escritor: CREA los recursos, lee lineas de stdin
 *                y las deja en la memoria compartida.
 *   ./shm g   -> lector: OBTIENE los recursos, imprime en stdout
 *                lo que el escritor deja y al final los elimina.
 *
 * Como son stdin/stdout, se combina con la shell:
 *   cat archivo.txt | ./shm c        (terminal 1)
 *   ./shm g > salida.txt             (terminal 2)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define KEY    0x1111      /* clave del conjunto de semaforos */
#define SHMKEY 5678        /* clave de la memoria compartida  */
#define MSGSZ  256

/*
 * Un conjunto de 2 semaforos. Cada uno es un "estado" del buffer:
 *   VACIO (0): vale 1 si el buffer esta libre para escribir.
 *   LLENO (1): vale 1 si hay un mensaje listo para leer.
 * Siempre se cumple que solo UNO de los dos vale 1:
 *   estado A = (VACIO=1, LLENO=0)  -> el escritor puede escribir
 *   estado B = (VACIO=0, LLENO=1)  -> el lector puede leer
 */
#define VACIO 0
#define LLENO 1
#define NSEMS 2

union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

union semun u;

/* Lo que vive dentro de la memoria compartida */
struct datos
{
    int  fin;              /* 1 = el escritor ya no enviara mas */
    char msg[MSGSZ];       /* el mensaje actual                 */
};

/*
 * semP / semV: igual que en semaphore.c, pero ahora reciben
 * el numero de semaforo dentro del conjunto (VACIO o LLENO).
 * P = esperar (resta 1, se bloquea si vale 0)
 * V = senalar (suma 1, despierta a quien espera)
 */
void semP(int id, int num)
{
    struct sembuf p = {num, -1, 0};
    if (semop(id, &p, 1) < 0)
    {
        perror("semop p");
        exit(13);
    }
}

void semV(int id, int num)
{
    struct sembuf v = {num, +1, 0};
    if (semop(id, &v, 1) < 0)
    {
        perror("semop v");
        exit(14);
    }
}

/*
 * getSemCreate: crea el conjunto de 2 semaforos y fija el estado
 * inicial con SETALL: VACIO=1 (se puede escribir), LLENO=0 (nada
 * que leer). Lo usa el escritor ('c').
 */
int getSemCreate()
{
    unsigned short inicial[NSEMS] = {1, 0};
    int i = semget(KEY, NSEMS, 0666 | IPC_CREAT);
    if (i < 0)
    {
        perror("semget");
        exit(11);
    }
    u.array = inicial;
    if (semctl(i, 0, SETALL, u) < 0)
    {
        perror("semctl");
        exit(12);
    }
    return i;
}

/* getSem: solo busca el conjunto que ya creo el escritor ('g'). */
int getSem()
{
    int i = semget(KEY, NSEMS, 0);
    if (i < 0)
    {
        perror("semget");
        exit(11);
    }
    return i;
}

/*
 * getShm: igual que shmget + shmat de shm_server.c / shm_client.c.
 * Si crear == 1 crea el segmento, si no solo lo localiza.
 * Devuelve el id por parametro para poder eliminarlo luego.
 */
struct datos *getShm(int crear, int *shmid)
{
    struct datos *d;
    int flags = crear ? (IPC_CREAT | 0666) : 0666;

    if ((*shmid = shmget(SHMKEY, sizeof(struct datos), flags)) < 0)
    {
        perror("shmget");
        exit(1);
    }
    if ((d = shmat(*shmid, NULL, 0)) == (struct datos *) -1)
    {
        perror("shmat");
        exit(1);
    }
    return d;
}

/*
 * escritor: lee stdin linea por linea. Por cada linea:
 *   P(VACIO)  -> espera a que el lector haya vaciado el buffer
 *   escribe en la memoria compartida
 *   V(LLENO)  -> avisa al lector que hay algo para leer
 * Al terminar stdin (EOF) manda un ultimo mensaje con fin = 1.
 */
void escritor(int id, struct datos *d)
{
    char linea[MSGSZ];

    while (fgets(linea, MSGSZ, stdin) != NULL)
    {
        linea[strcspn(linea, "\n")] = '\0';   /* quita el salto de linea */
        semP(id, VACIO);
        strcpy(d->msg, linea);
        d->fin = 0;
        semV(id, LLENO);
    }

    semP(id, VACIO);
    d->msg[0] = '\0';
    d->fin = 1;
    semV(id, LLENO);
}

/*
 * lector: el camino inverso.
 *   P(LLENO)  -> espera a que haya un mensaje
 *   lo imprime (a stdout, asi la shell puede redirigirlo)
 *   V(VACIO)  -> devuelve el buffer al escritor
 * Termina cuando llega fin = 1.
 */
void lector(int id, struct datos *d)
{
    for (;;)
    {
        semP(id, LLENO);
        if (d->fin)
            break;
        printf("%s\n", d->msg);
        fflush(stdout);
        semV(id, VACIO);
    }
}

int main(int argc, char *argv[])
{
    int id, shmid;
    struct datos *d;

    if (argc < 2 || (argv[1][0] != 'c' && argv[1][0] != 'g'))
    {
        fprintf(stderr, "uso: %s c|g\n", argv[0]);
        exit(2);
    }

    if (argv[1][0] == 'c')
    {
        id = getSemCreate();
        d = getShm(1, &shmid);
        fprintf(stderr, "escritor: sem=%d shm=%d\n", id, shmid);
        escritor(id, d);
        shmdt(d);
    }
    if (argv[1][0] == 'g')
    {
        id = getSem();
        d = getShm(0, &shmid);
        fprintf(stderr, "lector: sem=%d shm=%d\n", id, shmid);
        lector(id, d);
        shmdt(d);
        /* el lector limpia: borra memoria y semaforos del sistema */
        shmctl(shmid, IPC_RMID, NULL);
        semctl(id, 0, IPC_RMID);
    }
    return 0;
}
