# ⚓ Batalha Naval — Multiplayer via Sockets TCP em C

<div align="center">

![C](https://img.shields.io/badge/Linguagem-C-blue?style=for-the-badge&logo=c)
![Sockets](https://img.shields.io/badge/Rede-Sockets%20TCP-green?style=for-the-badge)
![Pthreads](https://img.shields.io/badge/Concorrência-Pthreads-orange?style=for-the-badge)
![Plataforma](https://img.shields.io/badge/Plataforma-Linux-yellow?style=for-the-badge&logo=linux)
![Status](https://img.shields.io/badge/Status-Concluído-brightgreen?style=for-the-badge)

**Jogo de Batalha Naval multijogador em tempo real, rodando sobre TCP/IP com arquitetura cliente-servidor, concorrência via POSIX threads e visualização em terminal com cores ANSI.**

</div>

---

## 📋 Índice

- [Sobre o Projeto](#-sobre-o-projeto)
- [Demonstração](#-demonstração)
- [Arquitetura](#-arquitetura)
- [Estrutura de Arquivos](#-estrutura-de-arquivos)
- [Protocolo de Comunicação](#-protocolo-de-comunicação)
- [Mecânicas do Jogo](#-mecânicas-do-jogo)
- [Matrizes de Visualização](#-matrizes-de-visualização)
- [Pré-requisitos](#-pré-requisitos)
- [Como Compilar e Executar](#-como-compilar-e-executar)
- [Decisões Técnicas](#-decisões-técnicas)
- [Possíveis Melhorias](#-possíveis-melhorias)
- [Autor](#-autor)

---

## 📖 Sobre o Projeto

Este projeto implementa o clássico jogo **Batalha Naval** para dois jogadores conectados em rede local (ou via loopback), totalmente em **linguagem C**. Foi desenvolvido como exercício prático de:

- **Programação de redes** com a API BSD Sockets
- **Concorrência** com POSIX Threads (`pthreads`) e exclusão mútua com `mutex`
- **Arquitetura cliente-servidor** com separação clara de responsabilidades
- **Protocolo de aplicação** textual customizado sobre TCP
- **Interface de terminal** com renderização de matrizes e cores ANSI

O código passou por uma etapa de **refatoração**, onde a lógica do jogo foi extraída para um módulo independente (`battleship.c / battleship.h`), eliminando duplicação e tornando o servidor mais enxuto e testável.

---

## 🎮 Demonstração

### Tela do cliente durante a partida

```
  === SEU TABULEIRO (DEFESA) ===        === TABULEIRO ADVERSÁRIO (ATAQUE) ===
      1  2  3  4  5  6  7  8                1  2  3  4  5  6  7  8
  1   ~  ~  ~  ~  ~  ~  ~  ~           1   ~  ~  ~  ~  ~  ~  ~  ~
  2   ~  N  ~  ~  ~  ~  ~  ~           2   ~  O  ~  ~  ~  ~  ~  ~
  3   ~  N  ~  ~  ~  ~  ~  ~           3   ~  ~  ~  X  ~  ~  ~  ~
  4   ~  ~  ~  N  N  N  ~  ~           4   ~  ~  ~  ~  ~  ~  ~  ~
  5   ~  ~  ~  ~  ~  ~  ~  ~           5   ~  ~  X  ~  ~  ~  ~  ~
  6   ~  ~  ~  ~  N  ~  ~  ~           6   ~  ~  #  #  ~  ~  ~  ~
  7   ~  ~  ~  ~  ~  ~  ~  ~           7   ~  ~  ~  ~  ~  ~  ~  ~
  8   ~  ~  ~  ~  ~  ~  ~  ~           8   ~  ~  ~  ~  ~  ~  ~  ~

  Legenda: N=Navio  X=Acerto  #=Afundado  O=Água  ~=Desconhecido
```

### Fluxo de uma partida

```
[Servidor]                        [Cliente 1]                   [Cliente 2]
    |                                  |                              |
    |<------- conexão TCP -------------|                              |
    |<------- conexão TCP ------------------------------------------|
    |------- "FASE DE POSICIONAMENTO" ->|                             |
    |                                  |-- JOIN Cris ----------------->|
    |                                  |                              |-- JOIN Bia -->
    |<--- POS DESTROYER 3 4 H ---------|                              |
    |<--- POS FRAGATA 1 1 H ------------|                              |
    |<--- READY ------------------------|                              |
    |                         (aguarda Bia ficar pronto)            |
    |<----------------------------------------------------------------- READY
    |------- "BATALHA COMEÇOU!" ------->|                             |
    |<--- FIRE 3 4 --------------------|                              |
    |------- "ACERTO! Turno de Bia" ->|                             |
    |                                  |                 (turno de Bia)
    |<----------------------------------------------------------------- FIRE 1 1
    |------- "ÁGUA! Turno de Cris" ----->|                             |
    ...
```

---

## 🏗 Arquitetura

O projeto segue o padrão **cliente-servidor com threads por conexão**:

```
┌─────────────────────────────────────────────────────────────┐
│                        SERVIDOR                             │
│                                                             │
│  main()                                                     │
│   └─ accept() loop ──> pthread_create() por cliente         │
│                              │                              │
│                    gerenciar_cliente()                       │
│                         │                                   │
│                    processar_comando()   ◄── battleship.c   │
│                         │                                   │
│              ┌──────────┴──────────┐                        │
│          Jogador 1             Jogador 2                    │
│         (Tabuleiro +           (Tabuleiro +                 │
│          mutex próprio)         mutex próprio)              │
│              │                      │                       │
│              └──── Jogo (mutex global, cond_prontos) ───────┘
└─────────────────────────────────────────────────────────────┘
         ▲                                    ▲
         │ TCP (porta 8080)                   │ TCP (porta 8080)
         ▼                                    ▼
┌──────────────────┐                ┌──────────────────┐
│    CLIENTE 1     │                │    CLIENTE 2     │
│                  │                │                  │
│ grade_ataque[][] │                │ grade_ataque[][] │
│ grade_defesa[][] │                │ grade_defesa[][] │
│                  │                │                  │
│ select() loop    │                │ select() loop    │
│  ├─ recv()       │                │  ├─ recv()       │
│  └─ stdin        │                │  └─ stdin        │
└──────────────────┘                └──────────────────┘
```

### Separação de responsabilidades

| Módulo | Responsabilidade |
|--------|-----------------|
| `battleship.c / .h` | Toda a lógica de jogo: tabuleiro, navios, tiros, turnos, vitória |
| `battleserver.c` | Rede: aceitar conexões, criar threads, fazer bridge entre clientes |
| `battleclient.c` | Interface: enviar comandos, receber mensagens, renderizar matrizes |
| `protocol.h` | Constantes do protocolo (comandos, tamanhos de buffer) |

---

## 📁 Estrutura de Arquivos

```
batalha_naval/
├── README.md
├── Makefile
├── common/
│   └── protocol.h          # Comandos e constantes do protocolo
├── battleship/
│   ├── battleship.h        # Tipos, structs e protótipos da lógica
│   └── battleship.c        # Implementação completa da lógica de jogo
├── server/
│   └── battleserver.c      # Servidor TCP multithreaded
└── client/
    └── battleclient.c      # Cliente com visualização em terminal
```

---

## 📡 Protocolo de Comunicação

O protocolo é **textual**, com comandos ASCII separados por `\n`, trafegando sobre TCP. Toda a comunicação é iniciada pelo cliente; o servidor envia confirmações e notificações de estado.

### Comandos do cliente → servidor

| Comando | Formato | Descrição |
|---------|---------|-----------|
| `JOIN` | `JOIN <nome>` | Entra na partida com o nome informado |
| `POS` | `POS <TIPO> <X> <Y> <H\|V>` | Posiciona um navio no tabuleiro |
| `READY` | `READY` | Sinaliza que terminou o posicionamento |
| `FIRE` | `FIRE <X> <Y>` | Dispara na coordenada do adversário |

### Respostas do servidor → cliente

| Mensagem | Significado |
|----------|------------|
| `=== BEM-VINDO, <nome>! VOCÊ É O JOGADOR N ===` | Confirmação de entrada + id do jogador |
| `*** <TIPO> posicionado em X,Y O (N/N navios) ***` | Navio posicionado com sucesso |
| `=== JOGADOR N (nome) ATACOU X Y: HIT ===` | Tiro que acertou um navio |
| `=== JOGADOR N (nome) ATACOU X Y: MISS ===` | Tiro que caiu na água |
| `=== JOGADOR N (nome) ATACOU X Y: SUNK ===` | Tiro que afundou um navio |
| `=== PARABÉNS <nome>! VOCÊ VENCEU! ===` | Anúncio de vitória |
| `END` | Sinal de encerramento da partida |

### Fases do protocolo

```
FASE 1 — CONEXÃO
  Ambos conectam e enviam JOIN.

FASE 2 — POSICIONAMENTO
  Cada jogador envia comandos POS até posicionar todos os navios,
  depois envia READY. O jogo inicia quando ambos estiverem prontos.

FASE 3 — COMBATE
  Jogadores alternam turnos enviando FIRE. O servidor processa,
  atualiza os tabuleiros e transmite o resultado para ambos.

FASE 4 — FIM
  Quando todos os navios de um jogador são afundados, o servidor
  anuncia o vencedor e envia END para ambos os clientes.
```

---

## ⚓ Mecânicas do Jogo

### Frota disponível

| Tipo | Tamanho | Quantidade |
|------|---------|-----------|
| SUBMARINO | 1 célula | 1 |
| FRAGATA | 2 células | 2 |
| DESTROYER | 3 células | 1 |
| **Total** | **8 células** | **4 navios** |

### Tabuleiro

- Grade **8×8**, com coordenadas de `1` a `8` em X e Y.
- Navios podem ser posicionados na **horizontal (H)** ou **vertical (V)**.
- Sobreposição de navios é **rejeitada** pelo servidor.
- Tiros fora do tabuleiro ou em células já atacadas são **rejeitados**.

### Representação interna da grade (servidor)

```
0   → célula vazia
>0  → navio intacto (valor = TipoNavio: 1, 2 ou 3)
<0  → célula atingida (valor = -TipoNavio)
```

Essa convenção permite identificar o tipo do navio atingido sem estruturas auxiliares.

### Lógica de turno

O servidor mantém o campo `turno_ativo` por jogador. Apenas o jogador com `turno_ativo == true` pode enviar `FIRE`; qualquer tentativa fora de turno recebe `ERRO: Não é o seu turno.`

---

## 🎯 Matrizes de Visualização

O cliente mantém **duas matrizes locais 8×8** que são atualizadas em tempo real conforme as mensagens chegam do servidor:

### `grade_defesa` — Seu tabuleiro

Mostra a posição dos seus navios e os tiros que o adversário fez em você.

- Preenchida localmente ao confirmar cada `POS` bem-sucedido.
- Atualizada para `X` ou `#` quando o servidor notifica que o adversário te acertou.

### `grade_ataque` — Tabuleiro adversário

Mostra o histórico de todos os tiros que você fez, sem revelar onde estão os navios do adversário.

- Atualizada para `O`, `X` ou `#` conforme o resultado do FIRE.

### Símbolos e cores ANSI

| Símbolo | Cor | Significado |
|---------|-----|------------|
| `~` | Cinza | Água / posição não atacada |
| `N` | Azul | Seu navio intacto |
| `O` | Ciano | Tiro caiu na água (miss) |
| `X` | Vermelho | Acerto em navio |
| `#` | Magenta | Navio completamente afundado |

As grades são **reimpresas a cada tiro**, mantendo o histórico visual completo da partida.

---

## ✅ Pré-requisitos

- Sistema operacional **Linux** (ou WSL no Windows)
- **GCC** — compilador C
- **Make** — para usar o Makefile
- Biblioteca **pthreads** (inclusa na glibc em praticamente todas as distribuições)

Para instalar em sistemas Debian/Ubuntu:

```bash
sudo apt update && sudo apt install build-essential
```

---

## 🚀 Como Compilar e Executar

### 1. Clone o repositório

```bash
git clone https://github.com/<seu-usuario>/batalha-naval.git
cd batalha-naval
```

### 2. Compile

```bash
make
```

Isso gera dois executáveis:
- `server/battleserver`
- `client/battleclient`

### 3. Inicie o servidor

```bash
./server/battleserver
```

```
[SERVIDOR] Batalha Naval iniciado na porta 8080. Aguardando jogadores...
```

### 4. Conecte os dois clientes (em terminais separados)

```bash
# Terminal 2
./client/battleclient

# Terminal 3
./client/battleclient
```

### 5. Jogue!

**Exemplo de sessão completa:**

```bash
# Ambos entram
> JOIN Bia
> JOIN Cris

# Posicionam os navios (cada um no seu terminal)
> POS SUBMARINO 5 5 H
> POS FRAGATA 1 1 H
> POS FRAGATA 3 3 V
> POS DESTROYER 7 2 H

# Ficam prontos
> READY

# Atacam (quando for a sua vez)
> FIRE 3 4
> FIRE 1 1
```

### Limpeza dos binários

```bash
make clean
```

---

## 🧠 Decisões Técnicas

### Thread por cliente com mutex granular

Cada cliente recebe uma thread dedicada (`pthread_create`). O acesso ao tabuleiro de cada jogador é protegido por um **mutex individual** (`tabuleiro.trava`), enquanto o estado global da partida usa um **mutex separado** (`jogo.mutex`). Isso minimiza contenção: dois tiros simultâneos em tabuleiros diferentes não bloqueiam um ao outro.

### Lógica isolada em módulo próprio

A separação de `battleship.c` do `battleserver.c` segue o princípio de responsabilidade única. O servidor cuida apenas de rede e threading; toda a validação de regras, posicionamento e combate vive no módulo de jogo. Isso facilita testes unitários e eventual reaproveitamento.

### Protocolo textual sobre TCP

A escolha por um protocolo textual (em vez de binário) simplifica o debug — é possível testar o servidor diretamente com `telnet` ou `nc`. A desvantagem de maior overhead é irrelevante para um jogo de turno com mensagens pequenas.

### `select()` no cliente

O cliente usa `select()` para monitorar simultaneamente o socket TCP e o `stdin`. Isso evita o uso de uma segunda thread no cliente e mantém a implementação simples, sem risco de condição de corrida na escrita do terminal.

### Matrizes locais no cliente

O cliente não consulta o servidor para renderizar os tabuleiros — apenas parseia as mensagens recebidas e atualiza estruturas locais. Isso mantém o servidor stateless em relação à apresentação e evita round-trips extras.

---

## 🔭 Possíveis Melhorias Futuras

- [ ] **Reconexão**: detectar desconexão e permitir que o jogador reconecte sem encerrar a partida
- [ ] **Modo espectador**: permitir conexões extras que apenas observam a partida
- [ ] **Modo LAN**: trocar `127.0.0.1` por argumento de linha de comando
- [ ] **Histórico de jogadas**: salvar o log de toda a partida em arquivo com replay
- [ ] **Interface TUI**: usar `ncurses` para uma interface mais elaborada com janelas separadas
- [ ] **Testes unitários**: adicionar suite de testes para `battleship.c` com checagem de colisões, vitória etc.
- [ ] **IPv6**: adicionar suporte a `AF_INET6`

---

## 👤 Autor

Desenvolvido por **[Cristiano Cardoso Cavalcante Lima]**

[![GitHub](https://img.shields.io/badge/GitHub-Cristiano--Cardoso-181717?style=flat&logo=github)](https://github.com/Cristiano1711)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Cristiano--Cardoso-0A66C2?style=flat&logo=linkedin)](https://linkedin.com/in/seu-perfil)

---

<div align="center">
  <sub>Feito com C, sockets e muita depuração de segmentation fault ☠️</sub>
</div>
