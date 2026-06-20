// Conexões do LCD no PORTD
sbit LCD_RS at LATD4_bit;
sbit LCD_EN at LATD5_bit;
sbit LCD_D4 at LATD0_bit;
sbit LCD_D5 at LATD1_bit;
sbit LCD_D6 at LATD2_bit;
sbit LCD_D7 at LATD3_bit;

sbit LCD_RS_Direction at TRISD4_bit;
sbit LCD_EN_Direction at TRISD5_bit;
sbit LCD_D4_Direction at TRISD0_bit;
sbit LCD_D5_Direction at TRISD1_bit;
sbit LCD_D6_Direction at TRISD2_bit;
sbit LCD_D7_Direction at TRISD3_bit;

#define LED_FORNO LATC0_bit

unsigned int temp_adc;
unsigned long temp_convertida;
unsigned long ultima_temp;

char linha1[17];
char linha2[17];
unsigned char centenas, dezenas, unidades, decimais;

volatile unsigned char modo_tempo;
volatile unsigned char rodando;
volatile unsigned char tempo_restante;
volatile unsigned char cont_250ms;

unsigned char btn_modo_estado;
unsigned char btn_start_estado;

unsigned char ultimo_modo;
unsigned char ultimo_tempo;

void interrupt() {
    if (TMR0IE_bit && TMR0IF_bit) {
        TMR0IF_bit = 0;
        TMR0H = 0xE1; TMR0L = 0x7C;
        if (tempo_restante > 0) tempo_restante--;
    }
    if (TMR1IE_bit && TMR1IF_bit) {
        TMR1IF_bit = 0;
        TMR1H = 0x0B; TMR1L = 0xDC;
        cont_250ms++;
        if (cont_250ms >= 4) {
            cont_250ms = 0;
            if (tempo_restante > 0) tempo_restante--;
        }
    }
}

unsigned int Ler_ADC_Manual() {
    unsigned int timeout = 0;
    Delay_us(20);
    GO_DONE_bit = 1;
    while(GO_DONE_bit == 1 && timeout < 500) timeout++;
    return (ADRESH << 8) | ADRESL;
}

void main() {
    modo_tempo = 0;
    rodando = 0;
    tempo_restante = 10;
    cont_250ms = 0;
    btn_modo_estado = 0;
    btn_start_estado = 0;
    ultimo_modo = 255;
    ultimo_tempo = 255;
    ultima_temp = 9999;
    temp_adc = 0;
    temp_convertida = 0;

    CMCON = 0x07;
    TRISA0_bit = 1; TRISA2_bit = 1; TRISA3_bit = 1;

    ADCON1 = 0b00011011;
    ADCON2 = 0b10111110;

    TRISC0_bit = 0; LED_FORNO = 0;
    TRISB0_bit = 1; TRISB1_bit = 1;

    INT0IE_bit = 0;  INT1IE_bit = 0;

    T0CON = 0b00000111;
    T1CON = 0b10110000;
    PEIE_bit = 1; GIE_bit = 1;

    // Inicialização Estável
    Lcd_Init();
    Delay_ms(100);
    Lcd_Cmd(_LCD_CLEAR);
    Delay_ms(50);
    Lcd_Cmd(_LCD_CURSOR_OFF); // Garante que o bloco preto desaparece

    Lcd_Out(1, 1, "A iniciar...    ");
    Delay_ms(800);
    // NÃO USAMOS MAIS CLEAR A PARTIR DAQUI. As variáveis sobrescrevem a tela limpa.

    TMR0IF_bit = 0; TMR1IF_bit = 0;

    while(1) {
        if (PORTB.F0 == 1 && btn_modo_estado == 0) {
            btn_modo_estado = 1;
            if (!rodando) {
                modo_tempo = !modo_tempo;
                if (modo_tempo == 0) tempo_restante = 10;
                else                 tempo_restante = 60;
            }
        }
        if (PORTB.F0 == 0) btn_modo_estado = 0;

        if (PORTB.F1 == 1 && btn_start_estado == 0) {
            btn_start_estado = 1;
            if (!rodando) {
                rodando = 1;
                ultimo_tempo = 255;
                ultima_temp = 9999;

                if (modo_tempo == 0) {
                    tempo_restante = 10;
                    cont_250ms = 0;
                    TMR1H = 0x0B; TMR1L = 0xDC;
                    TMR1ON_bit = 1; TMR1IE_bit = 1;
                } else {
                    tempo_restante = 60;
                    TMR0H = 0xE1; TMR0L = 0x7C;
                    TMR0ON_bit = 1; TMR0IE_bit = 1;
                }
            }
        }
        if (PORTB.F1 == 0) btn_start_estado = 0;

        ADCON1 = 0b00011011;
        ADCON0 = 0b00000001;
        temp_adc = Ler_ADC_Manual();
        temp_convertida = ((unsigned long)temp_adc * 1000) / 1023;

        if ((temp_convertida > ultima_temp + 2) || (ultima_temp > temp_convertida + 2) || ultima_temp == 9999) {
            centenas = (temp_convertida / 1000) % 10;
            dezenas  = (temp_convertida / 100) % 10;
            unidades = (temp_convertida / 10) % 10;
            decimais = temp_convertida % 10;

            linha1[0] = 'T'; linha1[1] = 'e'; linha1[2] = 'm'; linha1[3] = 'p'; linha1[4] = ':';
            linha1[5] = ' '; linha1[6] = ' ';

            if (centenas == 0) linha1[7] = ' ';
            else               linha1[7] = centenas + '0';

            linha1[8] = dezenas + '0';
            linha1[9] = unidades + '0';
            linha1[10] = '.';
            linha1[11] = decimais + '0';
            linha1[12] = 223;
            linha1[13] = 'C';
            linha1[14] = ' '; // Espaços limpam o resto da linha!
            linha1[15] = ' ';
            linha1[16] = '\0';

            if (centenas == 0 && dezenas == 0) linha1[8] = ' ';

            Lcd_Cmd(_LCD_CURSOR_OFF); // Reforço para ocultar o cursor
            Lcd_Out(1, 1, linha1);
            ultima_temp = temp_convertida;
        }

        if (!rodando) {
            LED_FORNO = 0;
            if (modo_tempo != ultimo_modo) {
                // A string tem exatos 16 caracteres. O que estiver por baixo será apagado.
                if (modo_tempo == 0) Lcd_Out(2, 1, "Modo: CURTO 10s ");
                else                 Lcd_Out(2, 1, "Modo: LONGO 60s ");
                ultimo_modo = modo_tempo;
            }
        } else {
            if (temp_convertida < 600) LED_FORNO = 1;
            else if (temp_convertida > 800) LED_FORNO = 0;

            if (tempo_restante != ultimo_tempo) {
                linha2[0] = 'T'; linha2[1] = 'e'; linha2[2] = 'm'; linha2[3] = 'p'; linha2[4] = 'o';
                linha2[5] = ':'; linha2[6] = ' ';
                linha2[7] = ' '; linha2[8] = ' ';
                linha2[9] = ' '; linha2[10] = ' ';
                linha2[11] = (tempo_restante / 10) + '0';
                linha2[12] = (tempo_restante % 10) + '0';
                linha2[13] = 's';
                linha2[14] = ' '; // Espaços limpam o resto da linha!
                linha2[15] = ' ';
                linha2[16] = '\0';

                if (linha2[11] == '0') linha2[11] = ' ';

                Lcd_Out(2, 1, linha2);
                ultimo_tempo = tempo_restante;
            }

            if (tempo_restante == 0) {
                rodando = 0;
                TMR0ON_bit = 0; TMR0IE_bit = 0;
                TMR1ON_bit = 0; TMR1IE_bit = 0;

                ultimo_modo = 255;
                ultima_temp = 9999;

                if (modo_tempo == 0) tempo_restante = 10;
                else                 tempo_restante = 60;
            }
        }
        Delay_ms(20);
    }
}