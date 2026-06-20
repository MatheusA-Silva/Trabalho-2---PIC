//Matheus Felipe Araujo da Silva - 14598171
sbit LCD_RS at LATD4_bit; // Define o pino de comando/dado (RS) no pino D4
sbit LCD_EN at LATD5_bit; // Define o pino de habilitação (Enable) no pino D5
sbit LCD_D4 at LATD0_bit; // Pino de dados D4 do LCD no pino D0 do PIC
sbit LCD_D5 at LATD1_bit; // Pino de dados D5 do LCD no pino D1 do PIC
sbit LCD_D6 at LATD2_bit; // Pino de dados D6 do LCD no pino D2 do PIC
sbit LCD_D7 at LATD3_bit; // Pino de dados D7 do LCD no pino D3 do PIC

// Configuração da direção dos pinos do LCD (0 = Saída, 1 = Entrada)
sbit LCD_RS_Direction at TRISD4_bit; // Define a direção do pino RS
sbit LCD_EN_Direction at TRISD5_bit; // Define a direção do pino EN
sbit LCD_D4_Direction at TRISD0_bit; // Define a direção do pino D4
sbit LCD_D5_Direction at TRISD1_bit; // Define a direção do pino D5
sbit LCD_D6_Direction at TRISD2_bit; // Define a direção do pino D6
sbit LCD_D7_Direction at TRISD3_bit; // Define a direção do pino D7

#define LED_FORNO LATC0_bit // Cria um apelido para o pino C0 (Controle do Forno/LED)

unsigned int temp_adc;           // Armazena o valor bruto de 10 bits do ADC (0 a 1023)
unsigned long temp_convertida;   // Armazena o valor escalonado (0 a 1000)
unsigned long ultima_temp;       // Armazena a última temperatura para a Histerese

char linha1[17];                 // Vetor de 17 posições para a Linha 1 do LCD (16 letras + \0)
char linha2[17];                 // Vetor de 17 posições para a Linha 2 do LCD (16 letras + \0)
unsigned char centenas, dezenas, unidades, decimais; // Variáveis para separar os algarismos

// O qualificador 'volatile' força o processador a ler o valor real da memória a cada
// ciclo, essencial para variáveis que são alteradas dentro da interrupção (Timers)
volatile unsigned char modo_tempo;     // 0 = Curto (10s), 1 = Longo (60s)
volatile unsigned char rodando;        // Flag: 0 = Parado/Menu, 1 = Contagem ativa
volatile unsigned char tempo_restante; // Guarda os segundos atuais da contagem regressiva
volatile unsigned char cont_250ms;     // Acumulador fracionário para o Timer1 (4x 250ms = 1s)

unsigned char btn_modo_estado;  // Memória de estado para o polling do botão Modo (Evita repetição)
unsigned char btn_start_estado; // Memória de estado para o polling do botão Start (Evita repetição)

unsigned char ultimo_modo;      // Guarda o último modo exibido na tela para evitar spam de escrita
unsigned char ultimo_tempo;     // Guarda o último segundo exibido para evitar spam de escrita

void interrupt() {
    // ---- TIMER 0: Responsável pela contagem do Modo Longo (60s) ----
    if (TMR0IE_bit && TMR0IF_bit) { // Se o Timer0 estiver ativado e a flag de estouro subir:
        TMR0IF_bit = 0;             // Abaixa a bandeira de estouro (obrigatoriedade de hardware)
        TMR0H = 0xE1; TMR0L = 0x7C; // Recarrega o Timer0 com 57724 (Estoura exatamente em 1 segundo a 8MHz)
        if (tempo_restante > 0) tempo_restante--; // Se ainda houver tempo, decrementa 1 segundo
    }

    // ---- TIMER 1: Responsável pela contagem do Modo Curto (10s) ----
    if (TMR1IE_bit && TMR1IF_bit) { // Se o Timer1 estiver ativado e a flag de estouro subir:
        TMR1IF_bit = 0;             // Abaixa a bandeira de estouro do Timer1
        TMR1H = 0x0B; TMR1L = 0xDC; // Recarrega o Timer1 com 3036 (Estoura exatamente a cada 250 milissegundos)
        cont_250ms++;               // Incrementa o contador de frações
        if (cont_250ms >= 4) {      // Se atingiu 4 frações de 250ms (Total = 1 segundo):
            cont_250ms = 0;         // Zera o contador de frações
            if (tempo_restante > 0) tempo_restante--; // Decrementa 1 segundo do painel
        }
    }
}

unsigned int Ler_ADC_Manual() {
    unsigned int timeout = 0; // Variável de segurança contra travamentos do simulador
    Delay_us(20);             // Tempo de aquisição: Aguarda o capacitor de retenção (CHOLD) carregar
    GO_DONE_bit = 1;          // Dispara o hardware de conversão do ADC

    // Fica em loop esperando a conversão terminar (GO_DONE ir para 0)
    // O timeout < 500 impede que o PIC congele para sempre caso o simulador trave o pino
    while(GO_DONE_bit == 1 && timeout < 500) timeout++;

    // Une os registradores ADRESH (High) e ADRESL (Low) deslocando os 8 bits mais
    // significativos para a esquerda e fazendo um OR bit a bit com os bits menos significativos
    return (ADRESH << 8) | ADRESL;
}

void main() {

    modo_tempo = 0;
    rodando = 0;
    tempo_restante = 10;
    cont_250ms = 0;
    btn_modo_estado = 0;
    btn_start_estado = 0;
    ultimo_modo = 255;  // Inicia com um valor irreal para forçar a primeira escrita na tela
    ultimo_tempo = 255; // Inicia com um valor irreal para forçar a primeira escrita na tela
    ultima_temp = 9999; // Inicia com um valor irreal para forçar a primeira escrita na tela
    temp_adc = 0;
    temp_convertida = 0;

    // ---- 2. CONFIGURAÇÕES DE HARDWARE (REGISTRADORES) ----
    CMCON = 0x07; // Desliga os comparadores analógicos internos (Evita conflitos nos pinos)

    // Configura os pinos RA0 (Sensor), RA2 e RA3 (VREF+) como entradas físicas
    TRISA0_bit = 1; TRISA2_bit = 1; TRISA3_bit = 1;

    // ADCON1 (PCFG = 1011): RA0 ao RA3 como Analógicos. (VCFG = 01): VREF+ externo, VREF- interno no GND.
    ADCON1 = 0b00011011;
    // ADCON2 (ADFM = 1): Justificado à direita. (ACQT = 111): 20 TAD. (ADCS = 110): Clock Fosc/64.
    ADCON2 = 0b10111110;

    TRISC0_bit = 0; // Configura RC0 como pino de Saída
    LED_FORNO = 0;  // Garante que o forno inicie desligado

    TRISB0_bit = 1; // Configura RB0 (Botão Modo) como Entrada
    TRISB1_bit = 1; // Configura RB1 (Botão Start) como Entrada

    // Desliga as interrupções externas físicas para usarmos polling (imune a ruído de contato)
    INT0IE_bit = 0;  INT1IE_bit = 0;

    T0CON = 0b00000111; // Timer0: 16 bits, Prescaler 1:256, Clock Interno. (Inicia desligado)
    T1CON = 0b10110000; // Timer1: 16 bits, Prescaler 1:8, Clock Interno. (Inicia desligado)

    PEIE_bit = 1; // Habilita interrupções de periféricos
    GIE_bit = 1;  // Habilita as interrupções globais do processador

    // ---- 3. INICIALIZAÇÃO DO DISPLAY LCD ----
    Lcd_Init();               // Prepara os pinos e a controladora do HD44780
    Delay_ms(100);            // Aguarda a estabilização elétrica do LCD
    Lcd_Cmd(_LCD_CLEAR);      // Limpa qualquer lixo da matriz de tela
    Delay_ms(50);             // Fôlego obrigatório para o comando CLEAR não encavalar
    Lcd_Cmd(_LCD_CURSOR_OFF); // Desliga o sublinhado/bloco piscante do cursor

    // Tela de Boot para confirmação visual de reinício limpo
    Lcd_Out(1, 1, "A iniciar...    ");
    Delay_ms(800);            // Mantém a tela por quase um segundo

    // Zera as flags de interrupção dos Timers preventivamente
    TMR0IF_bit = 0; TMR1IF_bit = 0;

    while(1) {

        // ---- POLLING DO BOTÃO DE MODO (Pino RB0) ----
        // Verifica se o botão foi pressionado (Nível Lógico 1) E se não estava pressionado antes
        if (PORTB.F0 == 1 && btn_modo_estado == 0) {
            btn_modo_estado = 1; // Trava o estado para registrar apenas UM clique
            if (!rodando) {      // Só permite alterar o modo se o forno estiver desligado
                modo_tempo = !modo_tempo; // Inverte o modo (0 vira 1, 1 vira 0)
                if (modo_tempo == 0) tempo_restante = 10; // Modo Curto
                else                 tempo_restante = 60; // Modo Longo
            }
        }
        // Se o pino for 0 (botão solto), destrava a variável permitindo um novo clique futuramente
        if (PORTB.F0 == 0) btn_modo_estado = 0;

        // ---- POLLING DO BOTÃO DE START (Pino RB1) ----
        if (PORTB.F1 == 1 && btn_start_estado == 0) {
            btn_start_estado = 1; // Trava o estado do botão Start
            if (!rodando) {       // Só dá start se o sistema não estiver rodando
                rodando = 1;      // Levanta a flag indicando que o forno ligou

                // Força valores irreais nas memórias para que a tela seja reescrita imediatamente
                ultimo_tempo = 255;
                ultima_temp = 9999;

                // Rotina de inicialização de qual Timer será usado
                if (modo_tempo == 0) {
                    tempo_restante = 10;        // Define o teto
                    cont_250ms = 0;             // Zera frações
                    TMR1H = 0x0B; TMR1L = 0xDC; // Carrega a carga inicial do Timer1
                    TMR1ON_bit = 1; TMR1IE_bit = 1; // Liga o Timer1 e habilita sua interrupção
                } else {
                    tempo_restante = 60;        // Define o teto
                    TMR0H = 0xE1; TMR0L = 0x7C; // Carrega a carga inicial do Timer0
                    TMR0ON_bit = 1; TMR0IE_bit = 1; // Liga o Timer0 e habilita sua interrupção
                }
            }
        }
        // Destrava o botão Start quando solto
        if (PORTB.F1 == 0) btn_start_estado = 0;

        // ---- LEITURA E TRATAMENTO DA TEMPERATURA ----
        ADCON1 = 0b00011011; // Reforça a configuração do VREF+ externo (Proteção)
        ADCON0 = 0b00000001; // Reforça a leitura no Canal AN0 e módulo ligado
        temp_adc = Ler_ADC_Manual(); // Faz a leitura do sensor

        // Escalonamento matemático sem o uso de variáveis "float".
        // Transforma o valor máximo (1023) em 1000 (Que representará 100.0 °C)
        temp_convertida = ((unsigned long)temp_adc * 1000) / 1023;

        // HISTERESE: O ecrã só é atualizado se a temperatura subir/cair mais de 0.2°C,
        // ou se for a primeira inicialização (9999). Evita que a tela fique piscando por ruído.
        if ((temp_convertida > ultima_temp + 2) || (ultima_temp > temp_convertida + 2) || ultima_temp == 9999) {

            // Separação de inteiros através de matemática de restos e divisões
            centenas = (temp_convertida / 1000) % 10;
            dezenas  = (temp_convertida / 100) % 10;
            unidades = (temp_convertida / 10) % 10;
            decimais = temp_convertida % 10;

            // Montagem manual do vetor de caracteres (Substitui conversões pesadas como sprintf)
            linha1[0] = 'T'; linha1[1] = 'e'; linha1[2] = 'm'; linha1[3] = 'p'; linha1[4] = ':';
            linha1[5] = ' '; linha1[6] = ' ';

            // Oculta a centena se ela for zero (Ex: Mostra " 25.0" em vez de "025.0")
            if (centenas == 0) linha1[7] = ' ';
            else               linha1[7] = centenas + '0'; // Soma com o char '0' converte inteiro em ASCII

            linha1[8] = dezenas + '0';
            linha1[9] = unidades + '0';
            linha1[10] = '.';
            linha1[11] = decimais + '0';
            linha1[12] = 223; // Código ASCII para o símbolo de Grau (°)
            linha1[13] = 'C';
            linha1[14] = ' '; // Estes espaços no final substituem o Lcd_Cmd(_LCD_CLEAR)
            linha1[15] = ' '; // Pois eles sobrepõem lixo antigo com caracteres em branco
            linha1[16] = '\0'; // Caractere nulo indicando o fim da string no C

            // Se a centena E a dezena forem zero (Ex: "005.0"), oculta a dezena também
            if (centenas == 0 && dezenas == 0) linha1[8] = ' ';

            Lcd_Cmd(_LCD_CURSOR_OFF); // Reitera o apagamento do cursor
            Lcd_Out(1, 1, linha1);    // Imprime o vetor pronto na Linha 1
            ultima_temp = temp_convertida; // Salva o valor atual para a histerese do próximo ciclo
        }

        // ---- LÓGICA DO ATUADOR E EXIBIÇÃO DA LINHA 2 ----
        if (!rodando) { // SE O FORNO ESTIVER OCIOSO (MENU)
            LED_FORNO = 0; // Garante pino desligado

            // Só atira os dados para o LCD se o usuário alterou a chave de modo
            if (modo_tempo != ultimo_modo) {
                // A string tem exatamente 16 caracteres para preencher toda a largura da tela
                if (modo_tempo == 0) Lcd_Out(2, 1, "Modo: CURTO 10s ");
                else                 Lcd_Out(2, 1, "Modo: LONGO 60s ");
                ultimo_modo = modo_tempo; // Atualiza a memória
            }

        } else { // SE O FORNO ESTIVER LIGADO (CONTAGEM ATIVA)

            // Controle Termostático (Mantém entre 60°C e 80°C baseando-se na escala de milhares)
            if (temp_convertida < 600) LED_FORNO = 1;       // Liga se < 60.0°C
            else if (temp_convertida > 800) LED_FORNO = 0;  // Desliga se > 80.0°C

            // O LCD só é atualizado caso o segundo real tenha decrescido na interrupção
            if (tempo_restante != ultimo_tempo) {
                linha2[0] = 'T'; linha2[1] = 'e'; linha2[2] = 'm'; linha2[3] = 'p'; linha2[4] = 'o';
                linha2[5] = ':'; linha2[6] = ' ';
                linha2[7] = ' '; linha2[8] = ' ';
                linha2[9] = ' '; linha2[10] = ' ';
                linha2[11] = (tempo_restante / 10) + '0'; // Extrai a dezena do tempo
                linha2[12] = (tempo_restante % 10) + '0'; // Extrai a unidade do tempo
                linha2[13] = 's';
                linha2[14] = ' ';
                linha2[15] = ' ';
                linha2[16] = '\0'; // Fim da string

                // Apaga o zero à esquerda se for menor que 10 segundos
                if (linha2[11] == '0') linha2[11] = ' ';

                Lcd_Out(2, 1, linha2); // Imprime a contagem regressiva
                ultimo_tempo = tempo_restante; // Salva para o próximo ciclo
            }

            // GATILHO DE FIM DO PROCESSO
            if (tempo_restante == 0) {
                rodando = 0; // Desliga a flag principal do processo

                // Desliga brutalmente os Timers e suas interrupções
                TMR0ON_bit = 0; TMR0IE_bit = 0;
                TMR1ON_bit = 0; TMR1IE_bit = 0;

                // "Estraga" as memórias para forçar o loop a redesenhar a tela inteira do Menu
                ultimo_modo = 255;
                ultima_temp = 9999;

                // Devolve a variável de tempo para o teto original preparatório para o próximo start
                if (modo_tempo == 0) tempo_restante = 10;
                else                 tempo_restante = 60;
            }
        }

        // Delay minúsculo no fim do loop infinito. Evita saturar o motor de clock do SimulIDE.
        Delay_ms(20);
    }
}
