#ifndef FORMIGOPOLIS_H
#define FORMIGOPOLIS_H
#define MAX_FILA 10
#define PRIORITY_MAX 1
#include <pthread.h>

typedef struct {
    char nome[30];
    char id;
    char grupo; // Letra para o grupo de prioridade (M, V, P, S)
    int prioridade; // Nível de prioridade atual da pessoa. Menor número = maior prioridade
    int prioridadeInicial; // Guarda a prioridade inicial da pessoa
    int ordemChegadaFila; // Contador sequencial de quando a pessoa entrou na fila (para desempate)
    int contInanição; // Contador para a lógica de inanição
} Pessoa;

// Estrutura para passar argumentos para cada thread de pessoa
typedef struct {
    Pessoa *pessoa; // Ponteiro para os dados da pessoa
    int numInteracoes; // Número de vezes que a pessoa tentará usar o caixa
} ThreadArgs;

// --- DEFINIÇÃO EXPLÍCITA DO MONITOR DO CAIXA ---
// Esta struct encapsula todas as variáveis compartilhadas e as primitivas de sincronização
// que juntas formam o "monitor" do caixa da lotérica.
typedef struct {
    pthread_mutex_t mutex; // O mutex (trava) do monitor
    pthread_cond_t cond; // A variável de condição do monitor
    Pessoa fila_espera[MAX_FILA]; // A fila de espera do caixa
    int fila_tam; // O tamanho atual da fila
    int ocupado; // Indica se o caixa está ocupado (1) ou livre (0)
    int contadorChegadaGlobal; // Contador sequencial para atribuir a ordem de chegada
} CaixaMonitor;

// Funções auxiliares
int grupo_precedencia(char grupo);

int comparar(Pessoa a, Pessoa b);

void montar_fila(CaixaMonitor *monitor, char *buffer, int tamanho);

// Gerente
void *rotina_gerente(void *arg);

// Operações do monitor
void esperar(CaixaMonitor *monitor, Pessoa *p);

void atendido_pelo_caixa(Pessoa *p);

void liberar(CaixaMonitor *monitor, Pessoa *p);

void vai_embora_para_casa(Pessoa *p);

// Thread de pessoa
void *rotina_pessoa(void *arg);

// Inicialização
void inicializa_caixa_monitor(CaixaMonitor *monitor);

#endif
