#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define MAX_FILA 10
#define PRIORITY_MAX 1 // A menor prioridade numérica (maior prioridade real)

// Estrutura que representa uma Pessoa (cliente da lotérica)
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
    pthread_mutex_t mutex;       // O mutex (trava) do monitor
    pthread_cond_t cond;         // A variável de condição do monitor
    Pessoa fila_espera[MAX_FILA]; // A fila de espera do caixa
    int fila_tam;                // O tamanho atual da fila
    int ocupado;                 // Indica se o caixa está ocupado (1) ou livre (0)
    int contadorChegadaGlobal; // Contador sequencial para atribuir a ordem de chegada
} CaixaMonitor;

// Uma instância global do nosso monitor do caixa
CaixaMonitor monitor_caixa;

// --- FUNÇÕES AUXILIARES ---

// Compara duas Pessoas para determinar a prioridade (menor número = maior prioridade)
// Em caso de empate na prioridade, usa o tempo de chegada (menor tempo = chegou antes)
int comparar(Pessoa a, Pessoa b) {
    if (a.prioridade != b.prioridade) {
        return a.prioridade - b.prioridade;
    }
    // Desempate agora usa ordem_chegada_fila ---
    return a.ordemChegadaFila - b.ordemChegadaFila;
}

// Monta a string da fila para impressão no formato "{fila:MVPS}"
void montar_fila(CaixaMonitor *monitor, char* buffer, int tamanho) {
    Pessoa ordenada[MAX_FILA];
    // Copia a fila atual do monitor para uma array temporária para ordenação
    memcpy(ordenada, monitor->fila_espera, sizeof(Pessoa) * monitor->fila_tam);

    // Ordena a cópia da fila usando o Bubble Sort e a função 'comparar'
    for (int i = 0; i < monitor->fila_tam - 1; i++) {
        for (int j = 0; j < monitor->fila_tam - i - 1; j++) {
            if (comparar(ordenada[j], ordenada[j+1]) > 0) {
                Pessoa temp = ordenada[j];
                ordenada[j] = ordenada[j+1];
                ordenada[j+1] = temp;
            }
        }
    }

    char grupos[5] = {0}; // Armazena os grupos já adicionados para evitar repetições
    int usados = 0;       // Contador de grupos únicos
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
    printf("%s está na fila do caixa %s\n", p->nome, fila_str);

    // Loop de espera: a pessoa espera até ser a de maior prioridade e o caixa estar livre
    while (monitor->ocupado || (monitor->fila_tam > 0 && monitor->fila_espera[indice_maior_prioridade(monitor)].id != p->id)) {
        // Lógica de Inanição: Se a pessoa está esperando e não é a de maior prioridade
        int meu_indice_na_fila = encontrar_pessoa_na_fila(monitor, p->id);
        if (meu_indice_na_fila != -1 && monitor->fila_espera[indice_maior_prioridade(monitor)].id != p->id) {
            monitor->fila_espera[meu_indice_na_fila].contInanição++;

            // Se atingiu 2 frustrações, aumenta a prioridade
            if (monitor->fila_espera[meu_indice_na_fila].contInanição >= 2) {
                if (monitor->fila_espera[meu_indice_na_fila].prioridade > PRIORITY_MAX) {
                    monitor->fila_espera[meu_indice_na_fila].prioridade--; // Aumenta prioridade
                    printf("--- %s atingiu %d frustrações! Prioridade aumentada para %d! ---\n",
                           monitor->fila_espera[meu_indice_na_fila].nome, monitor->fila_espera[meu_indice_na_fila].contInanição, monitor->fila_espera[meu_indice_na_fila].prioridade);
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
    printf("%s está sendo atendido(a) %s\n", p->nome, fila_str);
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
void* rotina_pessoa(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
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

    Pessoa pessoas_data[] = {
        {"Mamãe Maria",   '1', 'M', 1, 1, 0, 0},
        {"Papai Marcos",  '2', 'M', 1, 1, 0, 0},
        {"Vovó Vanda",    '3', 'V', 2, 2, 0, 0},
        {"Vovô Valter",   '4', 'V', 2, 2, 0, 0},
        {"Pedreiro Pedro",'5', 'P', 3, 3, 0, 0},
        {"Prima Paula",   '6', 'P', 3, 3, 0, 0},
        {"Sueli",         '7', 'S', 4, 4, 0, 0},
        {"Silas",         '8', 'S', 4, 4, 0, 0}
    };

    pthread_t threads[8];
    ThreadArgs args[8];

    for (int i = 0; i < 8; i++) {
        args[i].pessoa = &pessoas_data[i];
        args[i].numInteracoes = numInteracoes;
        pthread_create(&threads[i], NULL, rotina_pessoa, &args[i]);
    }

    for (int i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
    }

    // Destroi o mutex e a variável de condição do monitor no final
    pthread_mutex_destroy(&monitor_caixa.mutex);
    pthread_cond_destroy(&monitor_caixa.cond);

    printf("Fim da simulação.\n");
    return 0;
}