/*
 * procesador_lotes.c
 *
 * CONTEXTO: un servicio que procesa "lotes" de datos (por ejemplo, generar
 * reportes) uno tras otro, sin parar. Necesita reaccionar a lo que pasa
 * desde afuera mientras trabaja:
 *
 *   SIGINT  (Ctrl+C)  -> cancelar el lote ACTUAL y seguir con el siguiente
 *                        (como Ctrl+C en psql: cancela la consulta, no cierra
 *                        el programa)
 *   SIGALRM (timeout) -> si un lote tarda más de LIMITE_SEGUNDOS, abortarlo
 *                        (como un servidor que mata una petición colgada)
 *   SIGUSR1           -> pedir un reporte de estado (NO interrumpe el trabajo)
 *   SIGTERM           -> apagado ordenado: terminar el lote actual y salir
 *                        (es lo que envía `docker stop` o `systemctl stop`)
 *
 * Compilar: gcc -Wall -o procesador_lotes procesador_lotes.c
 */

#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

#define LIMITE_SEGUNDOS 5

/* Motivos que devolverá sigsetjmp cuando se produzca un salto */
enum { MOTIVO_NORMAL = 0, MOTIVO_CANCELADO = 1, MOTIVO_TIMEOUT = 2 };

/* El "checkpoint" del videojuego: el punto seguro al que volvemos */
static sigjmp_buf punto_seguro;

/* Banderas: los handlers solo las activan, el código normal las revisa */
static volatile sig_atomic_t trabajo_activo = 0;
static volatile sig_atomic_t pedir_estado   = 0;
static volatile sig_atomic_t pedir_cierre   = 0;

/* Contadores para el reporte de estado */
static volatile int lote_actual = 0;
static int completados = 0;
static int cancelados  = 0;
static int con_timeout = 0;

/* ------------------------------------------------------------------ */
/* HANDLERS                                                            */
/* Regla: hacen lo mínimo posible. Sin printf (no es async-signal-safe) */
/* ------------------------------------------------------------------ */

/* SIGINT: abandonar el lote en curso, sin importar en qué función esté */
void al_cancelar(int signo)
{
    (void)signo;
    if (trabajo_activo)                        /* solo saltamos si hay algo que cancelar */
        siglongjmp(punto_seguro, MOTIVO_CANCELADO);
}

/* SIGALRM: el lote se pasó del tiempo permitido */
void al_vencer_tiempo(int signo)
{
    (void)signo;
    siglongjmp(punto_seguro, MOTIVO_TIMEOUT);
}

/* SIGUSR1: solo levanta una bandera; el reporte se imprime después,
 * en un momento seguro. Aquí NO hace falta saltar. */
void al_pedir_estado(int signo)
{
    (void)signo;
    pedir_estado = 1;
}

/* SIGTERM: solo avisa "hay que cerrar"; el bucle principal decide cuándo */
void al_pedir_cierre(int signo)
{
    (void)signo;
    pedir_cierre = 1;
}

/* ------------------------------------------------------------------ */
/* TRABAJO (simulado). Imagina que estas funciones son llamadas        */
/* anidadas de 10 niveles con archivos abiertos y memoria reservada.   */
/* ------------------------------------------------------------------ */

static void leer_datos(int n)
{
    printf("  [lote %d] leyendo datos de la base...\n", n);
    sleep(1);
}

static void transformar(int n)
{
    printf("  [lote %d] transformando datos...\n", n);
    /* Cada tercer lote simula quedarse "colgado" (8 s > 5 s de límite) */
    sleep(n % 3 == 0 ? 8 : 1);
}

static void escribir_resultados(int n)
{
    printf("  [lote %d] escribiendo reporte en disco...\n", n);
    sleep(1);
}

static void procesar_lote(int n)
{
    leer_datos(n);
    transformar(n);
    escribir_resultados(n);
}

/* Lo que hay que ordenar cuando un lote se abandona a medias */
static void limpiar_recursos(void)
{
    printf("  Limpieza: cerrando archivos, liberando memoria,\n");
    printf("  borrando el reporte parcial y devolviendo la conexión a la BD.\n");
}

static void imprimir_estado(void)
{
    printf("\n=== ESTADO === completados: %d | cancelados: %d | timeouts: %d\n\n",
           completados, cancelados, con_timeout);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    signal(SIGINT,  al_cancelar);
    signal(SIGALRM, al_vencer_tiempo);
    signal(SIGUSR1, al_pedir_estado);
    signal(SIGTERM, al_pedir_cierre);

    printf("PID del servicio: %d\n", getpid());
    printf("Ctrl+C = cancelar lote | kill -USR1 <PID> = estado | kill -TERM <PID> = apagar\n\n");

    /*
     * CHECKPOINT. Se guarda ANTES del bucle, así que después de cualquier
     * salto el programa vuelve aquí y sigue con el siguiente lote.
     *
     * Primera vez -> devuelve 0 (MOTIVO_NORMAL)
     * Tras un salto -> devuelve el valor que pasó siglongjmp (1 o 2),
     *                  y así sabemos POR QUÉ volvimos.
     */
    int motivo = sigsetjmp(punto_seguro, 1);

    if (motivo == MOTIVO_CANCELADO) {
        printf("\n>>> Lote %d CANCELADO por el usuario (Ctrl+C)\n", lote_actual);
        cancelados++;
        limpiar_recursos();
    } else if (motivo == MOTIVO_TIMEOUT) {
        printf("\n>>> Lote %d ABORTADO: superó %d segundos\n", lote_actual, LIMITE_SEGUNDOS);
        con_timeout++;
        limpiar_recursos();
    }

    /* Estado limpio tras cualquier recuperación */
    alarm(0);
    trabajo_activo = 0;

    while (!pedir_cierre) {
        if (pedir_estado) {          /* SIGUSR1 llegó: momento seguro para imprimir */
            imprimir_estado();
            pedir_estado = 0;
        }

        lote_actual++;
        printf("--- Iniciando lote %d ---\n", lote_actual);

        trabajo_activo = 1;
        alarm(LIMITE_SEGUNDOS);      /* si tarda más, llega SIGALRM */
        procesar_lote(lote_actual);
        alarm(0);                    /* terminó a tiempo: cancelar el temporizador */
        trabajo_activo = 0;

        completados++;
        printf("--- Lote %d terminado OK ---\n\n", lote_actual);
    }

    printf("SIGTERM recibida: cierre ordenado.\n");
    imprimir_estado();
    return 0;
}
