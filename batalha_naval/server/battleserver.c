#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "../battleship/battleship.h"
#include "../common/protocol.h"

#define PORTA_SERVIDOR 8080

Jogo jogo;
static int socket_escuta = -1;

// Thread de cada cliente: fica em loop lendo comandos até o jogo terminar
void *gerenciar_cliente(void *arg) {
    Jogador *p = (Jogador *)arg;
    char buffer[MAX_MSG];

    printf("[SERVIDOR] Cliente conectado (socket %d)\n", p->sockfd);

    while (!jogo.jogo_terminado) {
        int bytes = recv(p->sockfd, buffer, MAX_MSG - 1, 0);
        if (bytes <= 0) {
            printf("[SERVIDOR] Cliente desconectado (socket %d)\n", p->sockfd);
            break;
        }
        buffer[bytes] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        if (strlen(buffer) > 0) {
            if (arquivo_log) {
                fprintf(arquivo_log, "JOGADOR %d -> %s\n", p->id_jogador, buffer);
                fflush(arquivo_log);
            }
            processar_comando(&jogo, p, buffer);
        }
    }

    close(p->sockfd);
    p->sockfd = -1;

    pthread_mutex_lock(&jogo.mutex);
    jogo.num_jogadores--;
    if (jogo.num_jogadores <= 0) {
        printf("[SERVIDOR] Todos os jogadores saíram. Encerrando.\n");
        jogo.jogo_terminado = true;
    }
    pthread_mutex_unlock(&jogo.mutex);

    return NULL;
}

int main(void) {
    socket_escuta = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_escuta == -1) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(socket_escuta, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in endereco = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(PORTA_SERVIDOR)
    };
    if (bind(socket_escuta, (struct sockaddr *)&endereco, sizeof(endereco)) == -1) {
        perror("bind"); exit(1);
    }
    if (listen(socket_escuta, 5) == -1) { perror("listen"); exit(1); }

    inicializar_jogo(&jogo);
    printf("[SERVIDOR] Batalha Naval iniciado na porta %d. Aguardando jogadores...\n",
           PORTA_SERVIDOR);

    arquivo_log = fopen("log_play.txt", "w");
    if (!arquivo_log) { perror("fopen"); exit(1); }
    fprintf(arquivo_log, "=== NOVA PARTIDA ===\n\n");
    fflush(arquivo_log);

    // Aceita exatamente MAX_CLIENTES jogadores antes de iniciar
    while (jogo.num_jogadores < MAX_CLIENTES) {
        int conexao = accept(socket_escuta, NULL, NULL);
        if (conexao == -1) { perror("accept"); continue; }

        if (!adicionar_jogador(&jogo, conexao)) {
            send(conexao, "ERRO: Partida lotada.\n", 22, 0);
            close(conexao);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, gerenciar_cliente,
                           &jogo.jogadores[jogo.num_jogadores - 1]) != 0) {
            perror("pthread_create");
            close(conexao);
            pthread_mutex_lock(&jogo.mutex);
            jogo.num_jogadores--;
            pthread_mutex_unlock(&jogo.mutex);
            continue;
        }
        pthread_detach(tid);
    }

    // Mantém o servidor ativo até o fim da partida
    while (!jogo.jogo_terminado) sleep(1);

    close(socket_escuta);
    fprintf(arquivo_log, "\n=== PARTIDA FINALIZADA ===\n");
    fclose(arquivo_log);
    printf("[SERVIDOR] Encerrado.\n");
    return 0;
}
