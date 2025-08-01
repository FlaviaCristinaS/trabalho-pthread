#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "formigopolis.h"
#define MAX_PEOPLE 8

// Uma instância global do nosso monitor do caixa
CaixaMonitor monitor_caixa;

// --- FUNÇÕES AUXILIARES ---

// Compara duas Pessoas para determinar a prioridade (menor número = maior prioridade)
// Em caso de empate na prioridade, usa o tempo de chegada (menor tempo = chegou antes)
// verifica pelo "grupo" de precedencia.
int grupo_precedencia(char grupo) {
    switch (grupo) {
        case 'M': return 0;
        case 'V': return 1;
        case 'P': return 2;
        case 'S': return 3;
        default: return 4;
    }
}
int comparar(Pessoa a, Pessoa b) {
    if (a.prioridade != b.prioridade)
        return a.prioridade - b.prioridade;

    // Empate de prioridade: usa ordem de chegada
    return a.ordemChegadaFila - b.ordemChegadaFila;
}


// Monta a string da fila para impressão no formato "{fila:MVPS}"
void montar_fila(CaixaMonitor *monitor, char *buffer, int tamanho) {
    Pessoa ordenada[MAX_FILA];
    // Copia a fila atual do monitor para uma array temporária para ordenação
    memcpy(ordenada, monitor->fila_espera, sizeof(Pessoa) * monitor->fila_tam);

    // Ordena a cópia da fila usando o Bubble Sort e a função 'comparar'
    for (int i = 0; i < monitor->fila_tam - 1; i++) {
        for (int j = 0; j < monitor->fila_tam - i - 1; j++) {
            if (comparar(ordenada[j], ordenada[j + 1]) > 0) {
                Pessoa temp = ordenada[j];
                ordenada[j] = ordenada[j + 1];
                ordenada[j + 1] = temp;
            }
        }
    }

    char grupos[5] = {0}; // Armazena os grupos já adicionados para evitar repetições
    int usados = 0; // Contador de grupos únicos
    int pos = snprintf(buffer, tamanho, "{fila:"); // Inicia a string
    // Adiciona os caracteres dos grupos únicos à string da fila
    for (int i = 0; i < monitor->fila_tam; i++) {
        char g = ordenada[i].grupo;
        int repetido = 0;
        for (int j = 0; j < usados; j++)
            if (grupos[j] == g) repetido = 1;
        if (!repetido) {
            grupos[usados++] = g;
            pos += snprintf(buffer + pos, tamanho - pos, "%c", g);
        }
    }
    snprintf(buffer + pos, tamanho - pos, "}"); // Fecha a string
}

// Retorna o índice de uma pessoa na fila de espera do monitor pelo ID
int encontrar_pessoa_na_fila(CaixaMonitor *monitor, char id) {
    for (int i = 0; i < monitor->fila_tam; i++) {
        if (monitor->fila_espera[i].id == id) {
            return i;
        }
    }
    return -1; // Não encontrou
}

// Adiciona uma Pessoa à fila de espera do monitor
void fila_adicionar(CaixaMonitor *monitor, Pessoa p) {
    if (monitor->fila_tam < MAX_FILA) {
        monitor->fila_espera[monitor->fila_tam++] = p;
    }
}

// Encontra o índice da pessoa com maior prioridade na fila do monitor
int indice_maior_prioridade(CaixaMonitor *monitor) {
    if (monitor->fila_tam == 0) return -1;
    int indice = 0;
    for (int i = 1; i < monitor->fila_tam; i++) {
        if (comparar(monitor->fila_espera[i], monitor->fila_espera[indice]) < 0) {
            indice = i;
        }
    }
    return indice;
}

// Remove a pessoa de maior prioridade da fila do monitor
void fila_remover_prioridade(CaixaMonitor *monitor) {
    int indice = indice_maior_prioridade(monitor);
    if (indice != -1) {
        for (int i = indice + 1; i < monitor->fila_tam; i++) {
            monitor->fila_espera[i - 1] = monitor->fila_espera[i];
        }
        monitor->fila_tam--;
    }
}

// --- OPERAÇÕES DO MONITOR (funções que interagem com o CaixaMonitor) ---

// Função 'esperar': Uma pessoa tenta entrar na fila e ser atendida
void esperar(CaixaMonitor *monitor, Pessoa *p) {
    // Bloqueia o mutex do monitor para acessar a seção crítica
    pthread_mutex_lock(&monitor->mutex);

    // Atribui e incrementa o contador sequencial de chegada ---
    p->ordemChegadaFila = monitor->contadorChegadaGlobal++;
    fila_adicionar(monitor, *p); // Adiciona a pessoa à fila do monitor

    char fila_str[50];
    montar_fila(monitor, fila_str, sizeof(fila_str));
    //verde para estar na fila
    printf("\033[1;32m%s está na fila do caixa %s\033[0m\n", p->nome, fila_str);

    // Loop de espera: a pessoa espera até ser a de maior prioridade e o caixa estar livre
    while (monitor->ocupado || (monitor->fila_tam > 0 && monitor->fila_espera[indice_maior_prioridade(monitor)].id != p
                                ->id)) {
        // Lógica de Inanição: Se a pessoa está esperando e não é a de maior prioridade
        int meu_indice_na_fila = encontrar_pessoa_na_fila(monitor, p->id);
        if (meu_indice_na_fila != -1 && monitor->fila_espera[indice_maior_prioridade(monitor)].id != p->id) {
            monitor->fila_espera[meu_indice_na_fila].contInanição++;

            // Se atingiu 2 frustrações, aumenta a prioridade
            if (monitor->fila_espera[meu_indice_na_fila].contInanição >= 2) {
                if (monitor->fila_espera[meu_indice_na_fila].prioridade > PRIORITY_MAX) {
                    monitor->fila_espera[meu_indice_na_fila].prioridade--; // Aumenta prioridade
                    // cor azul para indicar frustração
                    printf("\033[1;34m---> %s atingiu %d frustrações! Prioridade aumentada para (%d)! <---\033[0m\n",
                           monitor->fila_espera[meu_indice_na_fila].nome,
                           monitor->fila_espera[meu_indice_na_fila].contInanição,
                           monitor->fila_espera[meu_indice_na_fila].prioridade);
                    monitor->fila_espera[meu_indice_na_fila].contInanição = 0; // Reseta frustrações
                    pthread_cond_broadcast(&monitor->cond); // Sinaliza para reavaliar
                }
            }
        }
        // Libera o mutex e espera pelo sinal na variável de condição do monitor
        pthread_cond_wait(&monitor->cond, &monitor->mutex);
    }

    monitor->ocupado = 1; // Marca o caixa como ocupado
    fila_remover_prioridade(monitor); // Remove a pessoa da fila

    // Libera o mutex do monitor
    pthread_mutex_unlock(&monitor->mutex);
}

// Função 'atendido_pelo_caixa': Simula o tempo de atendimento
void atendido_pelo_caixa(Pessoa *p) {
    char fila_str[50];
    snprintf(fila_str, sizeof(fila_str), "{fila: }"); // Formato fixo para o exemplo do enunciado
    // amarelo para indicar que esta sendo atendido
    printf("\033[1;33m%s está sendo atendido(a) %s\033[0m\n", p->nome, fila_str);
    sleep(1); // Tempo de atendimento (1 segundo)
}

// Função 'liberar': Uma pessoa termina o atendimento e libera o caixa
void liberar(CaixaMonitor *monitor, Pessoa *p) {
    // Bloqueia o mutex do monitor
    pthread_mutex_lock(&monitor->mutex);

    // Lógica de Inanição: Reseta a prioridade da pessoa para a original
    if (p->prioridade != p->prioridadeInicial) {
        printf("--- %s foi atendido(a). Prioridade resetada para %d. ---\n", p->nome, p->prioridadeInicial);
        p->prioridade = p->prioridadeInicial;
        p->contInanição = 0; // Reseta frustrações
    } else {
        p->contInanição = 0; // Reseta frustrações mesmo se a prioridade não mudou
    }

    char fila_str[50];
    montar_fila(monitor, fila_str, sizeof(fila_str));
    printf("%s vai para casa %s\n", p->nome, fila_str);
    monitor->ocupado = 0; // Marca o caixa como livre
    pthread_cond_broadcast(&monitor->cond); // Acorda todas as threads esperando
    pthread_mutex_unlock(&monitor->mutex); // Libera o mutex
}

// Função 'vai_embora_para_casa': Simula o tempo fora da lotérica
void vai_embora_para_casa(Pessoa *p) {
    int tempo_aleatorio = (rand() % 3) + 3; // Tempo aleatório entre 3 e 5 segundos
    sleep(tempo_aleatorio);
}

// Função que cada thread de pessoa irá executar
void *rotina_pessoa(void *arg) {
    ThreadArgs *args = (ThreadArgs *) arg;
    Pessoa *p = args->pessoa; // Ponteiro para a Pessoa original
    int numInteracoes = args->numInteracoes;

    for (int i = 0; i < numInteracoes; ++i) {
        esperar(&monitor_caixa, p); // Passa o ponteiro para o monitor e para a pessoa
        atendido_pelo_caixa(p);
        liberar(&monitor_caixa, p); // Passa o ponteiro para o monitor e para a pessoa
        vai_embora_para_casa(p);
    }
    return NULL;
}

// Função para inicializar o monitor do caixa
void inicializa_caixa_monitor(CaixaMonitor *monitor) {
    monitor->fila_tam = 0;
    monitor->ocupado = 0;

    // Inicializa o novo contador de chegada ---
    monitor->contadorChegadaGlobal = 0;

    // Inicializa o mutex e a variável de condição explicitamente
    if (pthread_mutex_init(&monitor->mutex, NULL) != 0) {
        perror("Erro ao inicializar o mutex do monitor");
        exit(1);
    }
    if (pthread_cond_init(&monitor->cond, NULL) != 0) {
        perror("Erro ao inicializar a variável de condição do monitor");
        pthread_mutex_destroy(&monitor->mutex); // Limpa o mutex se a cond falhar
        exit(1);
    }
}

// Gerente

void* rotina_gerente(void* arg) {
    while (1) {
        sleep(5); // Checa a cada 5 segundos

        pthread_mutex_lock(&monitor_caixa.mutex);

        if (monitor_caixa.ocupado == 0) {
            int temM = 0, temV = 0, temP = 0;
            for (int i = 0; i < monitor_caixa.fila_tam; i++) {
                char g = monitor_caixa.fila_espera[i].grupo;
                if (g == 'M') temM = 1;
                else if (g == 'V') temV = 1;
                else if (g == 'P') temP = 1;
            }

            if (temM && temV && temP) {
                // Deadlock detectado colocando cor vermelha para ilustrar
                printf("\033[1;31m-->DEADLOCK detectado! Gerente irá liberar alguém!<--\033[0m\n");

                // Cria lista de índices válidos para sorter a galera
                int candidatos[MAX_FILA];
                int qtd = 0;
                for (int i = 0; i < monitor_caixa.fila_tam; i++) {
                    char g = monitor_caixa.fila_espera[i].grupo;
                    if (g == 'M' || g == 'V' || g == 'P') {
                        candidatos[qtd++] = i;
                    }
                }

                if (qtd > 0) {
                    int escolhido = candidatos[rand() % qtd];
                    Pessoa *p = &monitor_caixa.fila_espera[escolhido];
                    printf("+++ Gerente escolheu %s (grupo %c) para passar a frente fila! +++\n", p->nome, p->grupo);

                    // Move essa pessoa para o início da fila
                    Pessoa liberado = *p;
                    for (int i = escolhido; i > 0; i--) {
                        monitor_caixa.fila_espera[i] = monitor_caixa.fila_espera[i-1];
                    }
                    monitor_caixa.fila_espera[0] = liberado;

                    pthread_cond_broadcast(&monitor_caixa.cond);
                }
            }
        }

        pthread_mutex_unlock(&monitor_caixa.mutex);
    }

}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <numero_de_iteracoes>\n", argv[0]);
        return 1;
    }

    int numInteracoes = atoi(argv[1]);
    if (numInteracoes <= 0) {
        fprintf(stderr, "O número de iterações deve ser um inteiro positivo.\n");
        return 1;
    }

    srand(time(NULL)); // Inicializa o gerador de números aleatórios

    // Inicializa o monitor do caixa antes de criar as threads
    inicializa_caixa_monitor(&monitor_caixa);

    Pessoa pessoas_data[MAX_PEOPLE] = {
        {"Mamãe Maria",  '1', 'M'},
        {"Papai Marcos", '2', 'M'},
        {"Vovó Vanda",   '3', 'V'},
        {"Vovô Valter",  '4', 'V'},
        {"Pedreiro Pedro", '5', 'P'},
        {"Prima Paula",  '6', 'P'},
        {"Sueli",        '7', 'S'},
        {"Silas",        '8', 'S'}
    };

    // Inicializa campos extras
    for (int i = 0; i < MAX_PEOPLE; i++) {
        pessoas_data[i].prioridade = grupo_precedencia(pessoas_data[i].grupo) + 1; // 1 = maior prioridade
        pessoas_data[i].prioridadeInicial = pessoas_data[i].prioridade;
        pessoas_data[i].contInanição = 0;
        pessoas_data[i].ordemChegadaFila = 0;
    }

    pthread_t threads[MAX_PEOPLE];
    ThreadArgs args[MAX_PEOPLE];

    pthread_t thread_gerente;
    pthread_create(&thread_gerente, NULL, rotina_gerente, NULL);

    for (int i = 0; i < MAX_PEOPLE; i++) {
        args[i].pessoa = &pessoas_data[i];
        args[i].numInteracoes = numInteracoes;
        pthread_create(&threads[i], NULL, rotina_pessoa, &args[i]);
    }

    for (int i = 0; i < MAX_PEOPLE; i++) {
        pthread_join(threads[i], NULL);
    }

    // Destroi o mutex e a variável de condição do monitor no final
    pthread_mutex_destroy(&monitor_caixa.mutex);
    pthread_cond_destroy(&monitor_caixa.cond);

    printf("Fim da simulação.\n");
    return 0;
}
