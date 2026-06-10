#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "battleship.h"
#include "../common/protocol.h"

// Definição do arquivo de log (declarado extern em battleship.h)
FILE *arquivo_log = NULL;

/* ── helpers internos ────────────────────────────────────────────────── */

static TipoNavio analisar_tipo_navio(const char *s) {
    if (strcmp(s, "SUBMARINO") == 0) return SUBMARINO;
    if (strcmp(s, "FRAGATA")   == 0) return FRAGATA;
    if (strcmp(s, "DESTROYER") == 0) return DESTROYER;
    return 0;
}

static int contar_navios_do_tipo(Jogador *p, TipoNavio tipo) {
    int cont = 0;
    for (int i = 0; i < p->contagem_navios; i++)
        if (p->navios[i].tipo == tipo) cont++;
    return cont;
}

static Navio *obter_navio_na_coordenada(Jogador *p, Coordenada coord) {
    for (int i = 0; i < p->contagem_navios; i++) {
        Navio *n = &p->navios[i];
        if (!n->posicionado) continue;
        for (int j = 0; j < n->contagem_coords; j++)
            if (n->coordenadas[j].x == coord.x && n->coordenadas[j].y == coord.y)
                return n;
    }
    return NULL;
}

/* ── inicialização ───────────────────────────────────────────────────── */

void inicializar_jogo(Jogo *j) {
    pthread_mutex_init(&j->mutex, NULL);
    pthread_cond_init(&j->cond_prontos, NULL);
    j->num_jogadores  = 0;
    j->jogo_terminado = false;
    j->jogo_iniciado  = false;

    for (int i = 0; i < MAX_CLIENTES; i++) {
        Jogador *p = &j->jogadores[i];
        p->sockfd          = -1;
        memset(p->nome, 0, MAX_NOME_JOGADOR);
        p->entrou          = false;
        p->pronto          = false;
        p->turno_ativo     = false;
        p->contagem_navios = 0;
        pthread_mutex_init(&p->tabuleiro.trava, NULL);
        memset(p->tabuleiro.grade, 0, sizeof(p->tabuleiro.grade));
        for (int s = 0; s < TOTAL_NAVIOS; s++) {
            p->navios[s].posicionado = false;
            p->navios[s].acertos     = 0;
        }
    }
}

/* ── comunicação ─────────────────────────────────────────────────────── */

void enviar_para_jogador(Jogador *p, const char *msg) {
    if (p->sockfd != -1)
        send(p->sockfd, msg, strlen(msg), 0);
}

void transmitir_para_todos(const Jogo *j, const char *msg) {
    for (int i = 0; i < j->num_jogadores; i++)
        if (j->jogadores[i].sockfd != -1)
            send(j->jogadores[i].sockfd, msg, strlen(msg), 0);
    if (arquivo_log) { fprintf(arquivo_log, "%s", msg); fflush(arquivo_log); }
}

/* ── jogadores ───────────────────────────────────────────────────────── */

Jogador *buscar_jogador_pelo_socket(Jogo *j, int sockfd) {
    for (int i = 0; i < j->num_jogadores; i++)
        if (j->jogadores[i].sockfd == sockfd)
            return &j->jogadores[i];
    return NULL;
}

bool adicionar_jogador(Jogo *j, int sockfd) {
    pthread_mutex_lock(&j->mutex);
    if (j->num_jogadores >= MAX_CLIENTES) {
        pthread_mutex_unlock(&j->mutex);
        return false;
    }
    j->jogadores[j->num_jogadores].sockfd     = sockfd;
    j->jogadores[j->num_jogadores].id_jogador = j->num_jogadores + 1;
    j->num_jogadores++;
    pthread_mutex_unlock(&j->mutex);
    return true;
}

/* ── tabuleiro ───────────────────────────────────────────────────────── */

bool pode_posicionar(Jogador *p, TipoNavio tipo, Coordenada coord, Orientacao orient) {
    int tam = (int)tipo;
    for (int i = 0; i < tam; i++) {
        int x = coord.x + (orient == VERTICAL   ? i : 0);
        int y = coord.y + (orient == HORIZONTAL ? i : 0);
        if (x < 0 || x >= TAMANHO_TABULEIRO || y < 0 || y >= TAMANHO_TABULEIRO)
            return false;
        if (p->tabuleiro.grade[x][y] != 0)
            return false;
    }
    return true;
}

bool posicionar_navio(Jogador *p, TipoNavio tipo, Coordenada coord, Orientacao orient) {
    if (!pode_posicionar(p, tipo, coord, orient)) return false;
    int tam = (int)tipo;

    pthread_mutex_lock(&p->tabuleiro.trava);
    for (int i = 0; i < tam; i++) {
        int x = coord.x + (orient == VERTICAL   ? i : 0);
        int y = coord.y + (orient == HORIZONTAL ? i : 0);
        p->tabuleiro.grade[x][y] = tipo;
    }
    pthread_mutex_unlock(&p->tabuleiro.trava);

    Navio *n       = &p->navios[p->contagem_navios++];
    n->tipo        = tipo;
    n->tamanho     = tam;
    n->acertos     = 0;
    n->contagem_coords = tam;
    n->posicionado = true;
    for (int i = 0; i < tam; i++)
        n->coordenadas[i] = (Coordenada){
            coord.x + (orient == VERTICAL   ? i : 0),
            coord.y + (orient == HORIZONTAL ? i : 0)
        };
    return true;
}

/* ── lógica de combate ───────────────────────────────────────────────── */

void gerenciar_tiro(Jogo *j, Jogador *p, Coordenada coord) {
    Jogador *adv = (p == &j->jogadores[0]) ? &j->jogadores[1] : &j->jogadores[0];
    int resultado = 0; // 0=ÁGUA  1=ACERTO  2=AFUNDOU

    char coords_str[32];
    snprintf(coords_str, sizeof(coords_str), "%d %d", coord.x + 1, coord.y + 1);

    pthread_mutex_lock(&adv->tabuleiro.trava);
    if (coord.x < 0 || coord.x >= TAMANHO_TABULEIRO ||
        coord.y < 0 || coord.y >= TAMANHO_TABULEIRO ||
        adv->tabuleiro.grade[coord.x][coord.y] <= 0) {
        resultado = 0;
    } else {
        int tipo = adv->tabuleiro.grade[coord.x][coord.y];
        adv->tabuleiro.grade[coord.x][coord.y] = -tipo; // marca como atingido
        resultado = 1;
        Navio *n = obter_navio_na_coordenada(adv, coord);
        if (n) { n->acertos++; if (n->acertos >= n->tamanho) resultado = 2; }
    }
    pthread_mutex_unlock(&adv->tabuleiro.trava);

    const char *res_str = (resultado == 0) ? CMD_MISS
                        : (resultado == 1) ? CMD_HIT
                                           : CMD_SUNK;
    char msg[MAX_MSG];
    snprintf(msg, sizeof(msg), "=== JOGADOR %d (%s) ATACOU %s: %s ===\n",
             p->id_jogador, p->nome, coords_str, res_str);
    transmitir_para_todos(j, msg);

    Jogador *vencedor = NULL, *perdedor = NULL;
    if (verificar_vencedor(j, &vencedor, &perdedor)) {
        snprintf(msg, sizeof(msg), "=== PARABÉNS %s (JOGADOR %d)! VOCÊ VENCEU! ===\n",
                 vencedor->nome, vencedor->id_jogador);
        enviar_para_jogador(vencedor, msg);
        snprintf(msg, sizeof(msg), "=== QUE TRISTE! %s (JOGADOR %d) VOCÊ PERDEU! ===\n",
                 perdedor->nome, perdedor->id_jogador);
        enviar_para_jogador(perdedor, msg);
        if (arquivo_log) {
            fprintf(arquivo_log, "RESULTADO: %s (J%d) venceu; %s (J%d) perdeu\n\n",
                    vencedor->nome, vencedor->id_jogador,
                    perdedor->nome,  perdedor->id_jogador);
            fflush(arquivo_log);
        }
        transmitir_para_todos(j, "=== PARTIDA FINALIZADA ===\n");
        for (int i = 0; i < 2; i++)
            enviar_para_jogador(&j->jogadores[i], "END\n");
        j->jogo_terminado = true;
        return;
    }

    // Alterna turno
    p->turno_ativo   = false;
    adv->turno_ativo = true;
    snprintf(msg, sizeof(msg), "\n--- TURNO DO JOGADOR %d (%s) ---\n",
             adv->id_jogador, adv->nome);
    transmitir_para_todos(j, msg);
    enviar_para_jogador(adv, "*** SUA VEZ! Digite FIRE <x> <y> para atacar ***\n");
    enviar_para_jogador(p,   "*** AGUARDE O TURNO DO ADVERSÁRIO ***\n");
}

bool verificar_vencedor(Jogo *j, Jogador **vencedor, Jogador **perdedor) {
    if (!j->jogo_iniciado) return false;
    for (int i = 0; i < MAX_CLIENTES; i++) {
        bool tem_navio = false;
        pthread_mutex_lock(&j->jogadores[i].tabuleiro.trava);
        for (int x = 0; x < TAMANHO_TABULEIRO && !tem_navio; x++)
            for (int y = 0; y < TAMANHO_TABULEIRO; y++)
                if (j->jogadores[i].tabuleiro.grade[x][y] > 0) { tem_navio = true; break; }
        pthread_mutex_unlock(&j->jogadores[i].tabuleiro.trava);
        if (!tem_navio) {
            *perdedor = &j->jogadores[i];
            *vencedor = &j->jogadores[1 - i];
            return true;
        }
    }
    return false;
}

/* ── handlers de cada comando (privados) ────────────────────────────── */

static void processar_join(Jogo *j, Jogador *p, const char *cmd) {
    if (p->entrou) { enviar_para_jogador(p, "ERRO: Você já entrou na partida!\n"); return; }

    char nome[MAX_NOME_JOGADOR];
    if (sscanf(cmd + strlen(CMD_JOIN) + 1, "%31s", nome) != 1) {
        enviar_para_jogador(p, "ERRO: Use: JOIN <nome>\n");
        return;
    }
    strncpy(p->nome, nome, MAX_NOME_JOGADOR);
    p->entrou = true;

    char msg[MAX_MSG];
    snprintf(msg, sizeof(msg), "=== BEM-VINDO, %s! VOCÊ É O JOGADOR %d ===\n",
             p->nome, p->id_jogador);
    enviar_para_jogador(p, msg);

    if (j->num_jogadores == 1) {
        enviar_para_jogador(p, "*** AGUARDANDO OUTRO JOGADOR... ***\n");
    } else if (j->jogadores[0].entrou && j->jogadores[1].entrou) {
        transmitir_para_todos(j, "\n=== AMBOS CONECTADOS — FASE DE POSICIONAMENTO ===\n");
        transmitir_para_todos(j, "*** USE: POS <TIPO> <X> <Y> <H/V> ***\n");
    }
}

static void processar_pos(Jogo *j, Jogador *p, const char *cmd) {
    if (j->jogo_iniciado) { enviar_para_jogador(p, "ERRO: Partida já iniciada.\n"); return; }
    if (p->pronto)         { enviar_para_jogador(p, "ERRO: Você já está pronto.\n"); return; }

    char tipo_str[16]; int rx, ry; char oc;
    if (sscanf(cmd + strlen(CMD_POS) + 1, "%15s %d %d %c", tipo_str, &rx, &ry, &oc) != 4) {
        enviar_para_jogador(p, "ERRO: Use: POS <TIPO> <X> <Y> <H/V>\n");
        return;
    }
    if (rx < 1 || rx > TAMANHO_TABULEIRO || ry < 1 || ry > TAMANHO_TABULEIRO) {
        enviar_para_jogador(p, "ERRO: Coordenadas de 1 a 8.\n");
        return;
    }
    TipoNavio tipo = analisar_tipo_navio(tipo_str);
    if (!tipo) { enviar_para_jogador(p, "ERRO: Tipo inválido. Use SUBMARINO, FRAGATA ou DESTROYER.\n"); return; }

    int limite = (tipo == FRAGATA) ? 2 : 1;
    if (contar_navios_do_tipo(p, tipo) >= limite) {
        enviar_para_jogador(p, "ERRO: Limite deste tipo de navio já atingido.\n");
        return;
    }
    Coordenada coord = { rx - 1, ry - 1 };
    Orientacao orient = (oc == 'H' || oc == 'h') ? HORIZONTAL : VERTICAL;
    if (!posicionar_navio(p, tipo, coord, orient)) {
        enviar_para_jogador(p, "ERRO: Posição inválida ou já ocupada.\n");
        return;
    }
    char msg[MAX_MSG];
    snprintf(msg, sizeof(msg), "*** %s posicionado em %d,%d %c (%d/%d navios) ***\n",
             tipo_str, rx, ry, oc, p->contagem_navios, TOTAL_NAVIOS);
    enviar_para_jogador(p, msg);
    if (p->contagem_navios == TOTAL_NAVIOS)
        enviar_para_jogador(p, "*** Todos posicionados! Digite READY para começar ***\n");
}

static void processar_ready(Jogo *j, Jogador *p) {
    if (p->contagem_navios != TOTAL_NAVIOS) {
        char msg[MAX_MSG];
        snprintf(msg, sizeof(msg), "ERRO: Posicione todos os navios primeiro (%d/%d).\n",
                 p->contagem_navios, TOTAL_NAVIOS);
        enviar_para_jogador(p, msg);
        return;
    }
    if (p->pronto) { enviar_para_jogador(p, "ERRO: Você já está pronto.\n"); return; }

    p->pronto = true;
    char msg[MAX_MSG];
    snprintf(msg, sizeof(msg), "*** JOGADOR %d (%s) PRONTO! ***\n", p->id_jogador, p->nome);
    transmitir_para_todos(j, msg);

    pthread_mutex_lock(&j->mutex);
    bool ambos = j->jogadores[0].pronto && j->jogadores[1].pronto;
    pthread_mutex_unlock(&j->mutex);

    if (ambos) {
        transmitir_para_todos(j, "\n=== A BATALHA NAVAL COMEÇOU! ===\n");
        j->jogo_iniciado        = true;
        j->jogadores[0].turno_ativo = true;
        j->jogadores[1].turno_ativo = false;
        snprintf(msg, sizeof(msg), "\n--- TURNO DO JOGADOR 1 (%s) ---\n", j->jogadores[0].nome);
        transmitir_para_todos(j, msg);
        enviar_para_jogador(&j->jogadores[0], "*** SUA VEZ! FIRE <x> <y> ***\n");
    }
}

static void processar_fire(Jogo *j, Jogador *p, const char *cmd) {
    if (!j->jogo_iniciado) { enviar_para_jogador(p, "ERRO: Partida ainda não iniciada.\n"); return; }
    if (!p->turno_ativo)   { enviar_para_jogador(p, "ERRO: Não é o seu turno.\n"); return; }

    int rx, ry;
    if (sscanf(cmd + strlen(CMD_FIRE) + 1, "%d %d", &rx, &ry) != 2) {
        enviar_para_jogador(p, "ERRO: Use: FIRE <X> <Y>\n");
        return;
    }
    if (rx < 1 || rx > TAMANHO_TABULEIRO || ry < 1 || ry > TAMANHO_TABULEIRO) {
        enviar_para_jogador(p, "ERRO: Coordenadas de 1 a 8.\n");
        return;
    }
    gerenciar_tiro(j, p, (Coordenada){ rx - 1, ry - 1 });
}

/* ── dispatcher principal ────────────────────────────────────────────── */

void processar_comando(Jogo *j, Jogador *p, const char *comando) {
    if (j->jogo_terminado) return;

    // Copia e remove espaços do final
    char cmd[MAX_MSG];
    strncpy(cmd, comando, MAX_MSG - 1);
    cmd[MAX_MSG - 1] = '\0';
    char *fim = cmd + strlen(cmd) - 1;
    while (fim >= cmd && isspace((unsigned char)*fim)) *fim-- = '\0';

    printf("[DEBUG] Jogador %d: '%s'\n", p->id_jogador, cmd);

    if (strncmp(cmd, CMD_JOIN, strlen(CMD_JOIN)) == 0 &&
        isspace((unsigned char)cmd[strlen(CMD_JOIN)])) {
        processar_join(j, p, cmd);
        return;
    }
    if (!p->entrou) { enviar_para_jogador(p, "ERRO: Execute JOIN <nome> primeiro.\n"); return; }
    if (strcmp(cmd, CMD_READY) == 0)                         { processar_ready(j, p);     return; }
    if (strncmp(cmd, CMD_POS,  strlen(CMD_POS))  == 0)       { processar_pos(j, p, cmd);  return; }
    if (strncmp(cmd, CMD_FIRE, strlen(CMD_FIRE)) == 0)       { processar_fire(j, p, cmd); return; }

    enviar_para_jogador(p, "COMANDO INVÁLIDO! Use: JOIN, POS, READY ou FIRE\n");
}
