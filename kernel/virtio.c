#include <virtio.h>
#include <pmm.h>
#include <types.h>
#include <interrupts_handler.h>
#include <stdio.h>


#define VIRTIO0_BASE ((uint32_t *) 0x0A000000)

/* Offsets dos Registradores Virtio (em words / 4 bytes) */
#define VIRTIO_MAGIC                0x000 // 0x000 bytes
#define VIRTIO_VERSION              0x001 // 0x004 bytes
#define VIRTIO_DEVICE_ID            0x002 // 0x008 bytes
#define VIRTIO_STATUS               0x01C // 0x070 bytes
#define VIRTIO_QUEUE_SEL            0x00C // 0x030 bytes
#define VIRTIO_QUEUE_NUM_MAX        0x00D // 0x034 bytes
#define VIRTIO_QUEUE_NUM            0x00E // 0x038 bytes
#define VIRTIO_QUEUE_READY          0x011 // 0x044 bytes
#define VIRTIO_QUEUE_NOTIFY         0x014 // 0x050 bytes
#define VIRTIO_INTERRUPT_STATUS     0x018 // 0x060 bytes P.S: na documentação oficial esse valor esta como 0x60 por algum motivo
#define VIRTIO_INTERRUPT_ACK        0x019 // 0x64 bytes
#define VIRTIO_QUEUE_DESC_LO        0x020 // 0x080 bytes
#define VIRTIO_QUEUE_DESC_HI        0x021 // 0x084 bytes
#define VIRTIO_QUEUE_DRV_LO         0x024 // 0x090 bytes
#define VIRTIO_QUEUE_DRV_HI         0x025 // 0x094 bytes
#define VIRTIO_QUEUE_DEV_LO         0x028 // 0x0A0 bytes
#define VIRTIO_QUEUE_DEV_HI         0x029 // 0x0A4 bytes


/*Flags de Status do Virtio */
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FAILED      128

//IRQ
#define VIRTIO_NET_IRQ            48


/* Estruturas da Virtqueue */
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));


#define VIRTQ_SIZE 128

/* Estrutura para o kernel controlar o estado da fila */
struct virtqueue_info {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t last_used_idx; // Controla até onde o kernel já leu
    uint16_t avail_idx;     // Controla onde colocar o próximo buffer livre
};

// Filas globais (0 = RX, 1 = TX)
struct virtqueue_info virtqueues[2];


/*
Função interna para tratar a interrupção vinda da placa de rede.
*/
void virtio_net_irq_handler(void) {

    uint32_t *mmio = VIRTIO0_BASE;

    //Lê o registrador INTERRUPT_STATUS
    if(mmio[VIRTIO_INTERRUPT_STATUS] & 1){
        struct virtqueue_info *rx = &virtqueues[0];

        while(rx->last_used_idx != rx->used->idx){
            uint16_t used_ring_index = rx->last_used_idx % VIRTQ_SIZE;

            struct virtq_used_elem *elem = &rx->used->ring[used_ring_index];

            uint32_t desc_id = elem->id;  
            uint32_t pkt_len = elem->len;
            
            char *buffer_recebido = (char *)(uintptr_t) rx->desc[desc_id].addr;

            printf(">>> PACOTE DE REDE RECEBIDO<<<\n");

            //O virtio usa os 12 primeiro bytes do buffer para um cabeçalho proprio antes do da ethernet
            char *frame_ethernet = buffer_recebido + 12;

            //TODO: processar pacote

            uint16_t novo_avail_idx = rx->avail_idx % VIRTQ_SIZE;
            rx->avail->ring[novo_avail_idx] = desc_id;
            rx->avail_idx++;
            
            rx->last_used_idx++;
            
        }

        rx->avail->idx = rx->avail_idx;
        
        // Notifica a fila RX (offset 0x050 = Queue Notify)
        mmio[VIRTIO_QUEUE_NOTIFY] = 0;    
    }
    
    // Avisar a placa Virtio que a interrupção foi tratada escrevendo no registrador INTERRUPT_ACK.
    mmio[VIRTIO_INTERRUPT_ACK] = 1;
}


/*
Função interna para fazer configuração de uma fila
*/
void virtqueue_config(uint32_t *mmio, int32_t num){

    mmio[VIRTIO_QUEUE_SEL] = num; //Seleciona a fila
    uint32_t queue_max = mmio[VIRTIO_QUEUE_NUM_MAX];
    if (queue_max == 0) {
        // Fila nao suportada
        return;
    }
    uint32_t queue_size = VIRTQ_SIZE;
    mmio[VIRTIO_QUEUE_NUM] = queue_size;

    //Alocar as memorias da virtqueue

    virtqueues[num].desc  = (struct virtq_desc *) pmm_alloc_block();
    virtqueues[num].avail = (struct virtq_avail *) pmm_alloc_block();
    virtqueues[num].used  = (struct virtq_used *) pmm_alloc_block();
    virtqueues[num].last_used_idx = 0;
    virtqueues[num].avail_idx = 0;

    uintptr_t desc_addr = (uintptr_t) virtqueues[num].desc;
    uintptr_t avail_addr = (uintptr_t) virtqueues[num].avail;
    uintptr_t used_addr = (uintptr_t) virtqueues[num].used;

    // Informar os enderecos ao hardware (divididos em Low e High 32-bits) 
    mmio[VIRTIO_QUEUE_DESC_LO] = (uint32_t) desc_addr;
    mmio[VIRTIO_QUEUE_DESC_HI] = (uint32_t) (desc_addr >> 32);

    mmio[VIRTIO_QUEUE_DRV_LO]  = (uint32_t) avail_addr;
    mmio[VIRTIO_QUEUE_DRV_HI]  = (uint32_t) (avail_addr >> 32);

    mmio[VIRTIO_QUEUE_DEV_LO]  = (uint32_t) used_addr;
    mmio[VIRTIO_QUEUE_DEV_HI]  = (uint32_t) (used_addr >> 32);


    if (num == 0) { // Fila RX
        for (int i = 0; i < queue_size; i++) {
            // Aloca um buffer no heap do kernel
            void *buffer = kmalloc(2048); 
            
            // Configura o descritor apontando para o buffer
            virtqueues[num].desc[i].addr = (uint64_t)(uintptr_t)buffer;
            virtqueues[num].desc[i].len = 2048;
            virtqueues[num].desc[i].flags = 2; // VIRTQ_DESC_F_WRITE (Dispositivo pode escrever aqui)
            virtqueues[num].desc[i].next = 0;
            
            // Coloca o ID do descritor no anel disponível
            virtqueues[num].avail->ring[i] = i;
        }
        // Avisa a placa que disponibilizamos os índices de 0 a queue_size
        virtqueues[num].avail->idx = queue_size;
    }

    //Habilita a fila
    mmio[VIRTIO_QUEUE_READY] = 1;
}


void virtio_net_init(void){
    uint32_t *mmio = VIRTIO0_BASE; // Como dito na documentação, no futuro isso deve ser achado usando o device tree blob

    //Verificar Magic value para checar se realmente é o endereço do virtio

    if (mmio[VIRTIO_MAGIC] != 0x74726976){ // "virt" em ASCI
        printf("Nenhum dispositivo Virtio encontrado.\n");
        return;
    }
    //Vereficar versão e se é reconhecido como uma network card
    if (mmio[VIRTIO_VERSION] != 2){ 
        printf("Versão não suportada.\n");
        return;
    }
    
    if (mmio[VIRTIO_DEVICE_ID] != 1){  // 1 = network card
        printf("Dispositivo nao e uma placa de rede.\n");
        return;
    }


    /* 
    O processo de inicialização do virtio segue uma serie especifica de passos para avisar o driver
    de que ele vai ser usado como dispositivo de rede, os passos em sequência são:

    1.Virtio_status = 0 (Reinicio)
    2.Virtio_status = 1 (Acknowledge: O kernel reconhece o dispositivo virtio)
    3.virtio_status = 2 (Driver: O kernel diz que sabe como controlar o dispositivo)
    4. (O driver diz as configurações que vai usar para os pacotes, pulado neste kernel)
    


    O driver então verifica se o status é 8 indicando que o dispositivo aceitou as configurções.
    caso contrario, lança erro

    Depois disso, as virtqueues são configuradas
    neste programa, serão configuradas a queue 0 como recebimento RX e queue 1 como transmissão

    por sim, virtio_status recebe 4, onde o driver diz que terminou a configuração
    */

    
    mmio[VIRTIO_STATUS] = 0;
    mmio[VIRTIO_STATUS] |= VIRTIO_STATUS_ACKNOWLEDGE;
    mmio[VIRTIO_STATUS] |= VIRTIO_STATUS_DRIVER;
    mmio[VIRTIO_STATUS] |= VIRTIO_STATUS_FEATURES_OK;

    if (!(mmio[VIRTIO_STATUS] & VIRTIO_STATUS_FEATURES_OK)) {
        printf("Handshake com dispositivo falhou.\n");
        mmio[VIRTIO_STATUS] |= VIRTIO_STATUS_FAILED;
        return;
    }

    //Configuração da virtqueue
    virtqueue_config(mmio, 0); //Fila 0: RX
    virtqueue_config(mmio, 1); //Fila 1: TX

    mmio[VIRTIO_STATUS] |= VIRTIO_STATUS_DRIVER_OK;


    //registrar função de interrupção do IRQ
    if (register_interrupt_handler(VIRTIO_NET_IRQ, virtio_net_irq_handler) == 0) {
        printf("Driver Virtio registrado no GIC com sucesso!\n");
    } else {
        printf("Erro ao registrar IRQ do Virtio.\n");
    }

    //habilitar a interrupção no hardware do GIC
    gic_enable_interrupt(VIRTIO_NET_IRQ);
}