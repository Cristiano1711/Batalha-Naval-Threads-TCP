#ifndef BATTLESHIP_H
#define BATTLESHIP_H

#include <pthread.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include "../common/protocol.h"

#define TAMANHO_TABULEIRO 8
#define TOTAL_NAVIOS      4   // SUBMARINO(1) + FRAGATA(2) + DESTROYER(1)
#define MAX_NOME_JOGADOR  50
#define MAX_CLIENTES      2

// Arquivo de log — definido em battleship.c, usado também em battleserver.c
extern FILE *arquivo_log;

// Tipos de embarcação
typedef enum { SUBMARINO = 1, FRAGATA = 2, DESTROYER = 3 } TipoNavio;
typedef enum { HORIZONTAL, VERTICAL } Orientacao;

// Estrutura para coordenadas
typedef struct { int x, y; } Coordenada;

// Tabuleiro com proteção de mutex
typedef struct {
    int grade[TAMANHO_TABULEIRO][TAMANHO_TABULEIRO]; // 0=vazio, >0=intacto, <0=atingido
    pthread_mutex_t trava;
} Tabuleiro;

// Representação de uma embarcação
typedef struct {
    TipoNavio  tipo;
    int        tamanho;
    int        acertos;
    Coordenada coordenadas[TAMANHO_TABULEIRO];
    int        contagem_coords;
    bool       posicionado;
} Navio;

// Estado individual de cada jogador
typedef struct {
    int    sockfd;
    char   nome[MAX_NOME_JOGADOR];
    int    id_jogador;
    bool   entrou;
    Navio  navios[TOTAL_NAVIOS];
    int    contagem_navios;
    Tabuleiro tabuleiro;
    bool   pronto;
    bool   turno_ativo;
} Jogador;

// Estado geral da partida
typedef struct {
    Jogador        jogadores[MAX_CLIENTES];
    int            num_jogadores;
    pthread_mutex_t mutex;
    pthread_cond_t  cond_prontos;
    bool           jogo_terminado;
    bool           jogo_iniciado;
} Jogo;

// Inicialização
void inicializar_jogo(Jogo *jogo);

// Gerenciamento de jogadores
bool     adicionar_jogador(Jogo *jogo, int sockfd);
Jogador *buscar_jogador_pelo_socket(Jogo *jogo, int sockfd);

// Comunicação
void enviar_para_jogador(Jogador *jogador, const char *msg);
void transmitir_para_todos(const Jogo *jogo, const char *msg);

// Tabuleiro
bool pode_posicionar(Jogador *jogador, TipoNavio tipo, Coordenada coord, Orientacao orient);
bool posicionar_navio(Jogador *jogador, TipoNavio tipo, Coordenada coord, Orientacao orient);

// Lógica de jogo
void gerenciar_tiro(Jogo *jogo, Jogador *jogador, Coordenada coord);
bool verificar_vencedor(Jogo *jogo, Jogador **vencedor, Jogador **perdedor);

// Processamento de comandos
void processar_comando(Jogo *jogo, Jogador *jogador, const char *comando);

#endif // BATTLESHIP_H
