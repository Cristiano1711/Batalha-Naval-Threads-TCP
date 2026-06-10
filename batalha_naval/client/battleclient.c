#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <ctype.h>

#include "battleship.h"
#include "../common/protocol.h"

#define PORTA_SERVIDOR 8080
#define IP_SERVIDOR "127.0.0.1"

/* ── Matrizes de visualização ──────────────────────────────────────── */

// Células possíveis
#define CELULA_AGUA       '~'  // desconhecido / água não atirada
#define CELULA_NAVIO      'N'  // navio próprio intacto
#define CELULA_ERRO       'O'  // tiro na água (miss)
#define CELULA_ACERTO     'X'  // acerto em navio
#define CELULA_AFUNDADO   '#'  // navio afundado

// grade_ataque  : o que o jogador fez no tabuleiro adversário
// grade_defesa  : o próprio tabuleiro, mostrando navios e tiros recebidos
static char grade_ataque[TAMANHO_TABULEIRO][TAMANHO_TABULEIRO];
static char grade_defesa[TAMANHO_TABULEIRO][TAMANHO_TABULEIRO];

static void inicializar_grades() {
    for (int i = 0; i < TAMANHO_TABULEIRO; i++)
        for (int j = 0; j < TAMANHO_TABULEIRO; j++) {
            grade_ataque[i][j] = CELULA_AGUA;
            grade_defesa[i][j] = CELULA_AGUA;
        }
}

/* Registra um navio posicionado no tabuleiro de defesa */
static void marcar_navio_defesa(int x, int y, int tamanho, char orientacao) {
    for (int k = 0; k < tamanho; k++) {
        int cx = x + (orientacao == 'V' ? k : 0);
        int cy = y + (orientacao == 'H' ? k : 0);
        if (cx >= 0 && cx < TAMANHO_TABULEIRO &&
            cy >= 0 && cy < TAMANHO_TABULEIRO)
            grade_defesa[cx][cy] = CELULA_NAVIO;
    }
}

/* Cor ANSI conforme o símbolo da célula */
static const char *cor_celula(char c) {
    switch (c) {
        case CELULA_NAVIO:    return "\033[34m"; // azul
        case CELULA_ERRO:     return "\033[36m"; // ciano
        case CELULA_ACERTO:   return "\033[31m"; // vermelho
        case CELULA_AFUNDADO: return "\033[35m"; // magenta
        default:              return "\033[90m"; // cinza (água)
    }
}

static void imprimir_grades() {
    printf("\n");
    printf("  %-34s    %s\n",
           "=== SEU TABULEIRO (DEFESA) ===",
           "=== TABULEIRO ADVERSÁRIO (ATAQUE) ===");

    // Cabeçalho das colunas
    printf("     ");
    for (int j = 1; j <= TAMANHO_TABULEIRO; j++) printf(" %d ", j);
    printf("        ");
    for (int j = 1; j <= TAMANHO_TABULEIRO; j++) printf(" %d ", j);
    printf("\n");

    for (int i = 0; i < TAMANHO_TABULEIRO; i++) {
        // Tabuleiro de defesa
        printf("  %d  ", i + 1);
        for (int j = 0; j < TAMANHO_TABULEIRO; j++) {
            char c = grade_defesa[i][j];
            printf("%s %c \033[0m", cor_celula(c), c);
        }
        printf("    ");
        // Tabuleiro de ataque
        printf("  %d  ", i + 1);
        for (int j = 0; j < TAMANHO_TABULEIRO; j++) {
            char c = grade_ataque[i][j];
            printf("%s %c \033[0m", cor_celula(c), c);
        }
        printf("\n");
    }

    printf("\n");
    printf("  Legenda: \033[34mN\033[0m=Navio  "
           "\033[31mX\033[0m=Acerto  "
           "\033[35m#\033[0m=Afundado  "
           "\033[36mO\033[0m=Água  "
           "\033[90m~\033[0m=Desconhecido\n\n");
}

/* ── Parsing de mensagens do servidor ─────────────────────────────── */

/*
 * Tenta extrair coordenadas de uma mensagem do tipo:
 *   "=== JOGADOR N (nome) ATACOU X Y: RESULTADO ==="
 * Retorna 1 se conseguiu, 0 caso contrário.
 */
static int extrair_coordenadas_ataque(const char *msg, int *x, int *y,
                                      char *resultado) {
    int jogador_id;
    char nome[64];
    char res[16];

    if (sscanf(msg, "=== JOGADOR %d (%63[^)]) ATACOU %d %d: %15s",
               &jogador_id, nome, x, y, res) == 5) {
        strncpy(resultado, res, 15);
        resultado[15] = '\0';
        // Remove " ===" do final se houver
        char *p = strstr(resultado, " ===");
        if (p) *p = '\0';
        // x e y chegam base-1 da mensagem do servidor
        (*x)--; (*y)--;
        return jogador_id; // retorna o id de quem atirou
    }
    return 0;
}

/* Atualiza as grades com base na mensagem recebida */
static void atualizar_grades(const char *msg, int id_proprio) {
    int x, y, jogador_id;
    char resultado[16];

    jogador_id = extrair_coordenadas_ataque(msg, &x, &y, resultado);
    if (!jogador_id) return;

    // Determina símbolo
    char simbolo = CELULA_ERRO;
    bool afundou = (strncmp(resultado, "SUNK", 4) == 0);
    bool acerto  = (strncmp(resultado, "HIT",  3) == 0) || afundou;
    if (acerto && !afundou) simbolo = CELULA_ACERTO;
    else if (afundou)       simbolo = CELULA_AFUNDADO;
    else                    simbolo = CELULA_ERRO;

    if (x < 0 || x >= TAMANHO_TABULEIRO || y < 0 || y >= TAMANHO_TABULEIRO)
        return;

    if (jogador_id == id_proprio) {
        // Eu atirei: atualiza grade de ataque
        grade_ataque[x][y] = simbolo;
    } else {
        // Adversário atirou em mim: atualiza grade de defesa
        if (grade_defesa[x][y] == CELULA_NAVIO) {
            grade_defesa[x][y] = afundou ? CELULA_AFUNDADO : CELULA_ACERTO;
        } else if (grade_defesa[x][y] == CELULA_AGUA) {
            grade_defesa[x][y] = CELULA_ERRO;
        }
    }
}

/* Tenta identificar o id do próprio jogador a partir da mensagem de boas-vindas */
static int extrair_id_proprio(const char *msg) {
    int id = 0;
    sscanf(msg, "=== BEM-VINDO, %*[^!]! VOCÊ É O JOGADOR %d", &id);
    return id;
}

/* Tenta detectar um POS bem-sucedido para marcar no tabuleiro de defesa */
static void processar_confirmacao_pos(const char *msg, const char *ultimo_pos) {
    // Mensagem de confirmação: "*** TIPO posicionado em X,Y O (N/N navios) ***"
    // Combinamos com o último comando POS enviado pelo usuário, que guardamos.
    if (!strstr(msg, "posicionado em")) return;

    // ultimo_pos: "POS TIPO X Y O"
    char tipo[16]; int x, y; char orient;
    if (sscanf(ultimo_pos, "POS %15s %d %d %c", tipo, &x, &y, &orient) == 4) {
        int tamanho = 1;
        if (strcmp(tipo, "FRAGATA")   == 0) tamanho = 2;
        if (strcmp(tipo, "DESTROYER") == 0) tamanho = 3;
        marcar_navio_defesa(x - 1, y - 1, tamanho, toupper(orient));
    }
}

/* ── Interface ────────────────────────────────────────────────────── */

void imprimir_instrucoes() {
    printf("\n=== BATALHA NAVAL ===\n");
    printf("COMANDOS:\n");
    printf("  JOIN <NOME>               - ENTRAR NA PARTIDA\n");
    printf("  POS <TIPO> <X> <Y> <H/V> - POSICIONAR NAVIO\n");
    printf("  READY                     - INFORMA QUE ESTÁ PRONTO\n");
    printf("  FIRE <x> <y>              - ATIRAR NO ADVERSÁRIO\n");
    printf("\nTIPOS DE NAVIOS:\n");
    printf("  SUBMARINO (TAMANHO 1) - 1 NAVIO\n");
    printf("  FRAGATA   (TAMANHO 2) - 2 NAVIOS\n");
    printf("  DESTROYER (TAMANHO 3) - 1 NAVIO\n");
    printf("\nCOORDENADAS X | Y: 1 a 8\n");
    printf("ORIENTAÇÃO: H (HORIZONTAL) ou V (VERTICAL)\n");
    printf("========================\n\n");
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in servidor_addr = {0};
    servidor_addr.sin_family = AF_INET;
    servidor_addr.sin_port   = htons(PORTA_SERVIDOR);
    inet_pton(AF_INET, IP_SERVIDOR, &servidor_addr.sin_addr);

    printf("TENTANDO CONECTAR AO SERVIDOR...\n");
    if (connect(sock, (struct sockaddr *)&servidor_addr,
                sizeof(servidor_addr)) < 0) {
        perror("FALHA AO CONECTAR");
        close(sock);
        return 1;
    }

    printf("CONEXÃO COM O SERVIDOR ESTABELECIDA\n");
    imprimir_instrucoes();

    inicializar_grades();
    imprimir_grades();

    int    id_proprio   = 0;
    char   ultimo_pos[MAX_MSG] = {0}; // último comando POS digitado
    bool   partida_encerrada = false;

    printf("> ");
    fflush(stdout);

    fd_set fds;
    char buffer[MAX_MSG];

    while (!partida_encerrada) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(STDIN_FILENO, &fds);

        if (select(sock + 1, &fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        /* ── Mensagem do servidor ── */
        if (FD_ISSET(sock, &fds)) {
            ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) { printf("A CONEXÃO COM O SERVIDOR FOI PERDIDA.\n"); break; }
            buffer[n] = '\0';
            if (n > 0 && buffer[n - 1] == '\n') buffer[n - 1] = '\0';

            printf("\r%s\n", buffer);

            // Detecta id próprio
            if (!id_proprio) {
                int id = extrair_id_proprio(buffer);
                if (id) id_proprio = id;
            }

            // Atualiza tabuleiros com base em tiros
            if (strstr(buffer, "ATACOU")) {
                atualizar_grades(buffer, id_proprio);
                imprimir_grades();
            }

            // Confirma posicionamento de navio
            if (strstr(buffer, "posicionado em") && strlen(ultimo_pos) > 0) {
                processar_confirmacao_pos(buffer, ultimo_pos);
                imprimir_grades();
            }

            // Fim de jogo
            if (strstr(buffer, "VENCEU") || strstr(buffer, "PERDEU") ||
                strstr(buffer, "FINALIZADA") || strstr(buffer, "END")) {
                printf("\nAPERTE ENTER PARA SAIR\n");
                partida_encerrada = true;
                int c;
                while ((c = getchar()) != '\n' && c != EOF);
                continue;
            }

            // Prompt
            if (strstr(buffer, "SUA VEZ") || strstr(buffer, "Digite") ||
                strstr(buffer, "COMANDO") || strstr(buffer, "ERRO")    ||
                strstr(buffer, "AGUARDE") || strstr(buffer, "PRONTO")  ||
                strstr(buffer, "posicionado") || strstr(buffer, "READY")) {
                printf("\n> ");
            } else {
                printf("> ");
            }
            fflush(stdout);
        }

        /* ── Input do usuário ── */
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (!fgets(buffer, sizeof(buffer), stdin)) break;
            if (partida_encerrada) break;

            buffer[strcspn(buffer, "\n")] = '\0';
            if (strlen(buffer) == 0) { printf("> "); fflush(stdout); continue; }

            // Guarda último POS para processar confirmação
            if (strncasecmp(buffer, "POS ", 4) == 0)
                strncpy(ultimo_pos, buffer, MAX_MSG - 1);

            strcat(buffer, "\n");
            if (send(sock, buffer, strlen(buffer), 0) < 0) {
                perror("FALHA NO ENVIO DO COMANDO");
                break;
            }
        }
    }

    close(sock);
    printf("DESCONECTADO DO SERVIDOR!\n");
    return 0;
}
