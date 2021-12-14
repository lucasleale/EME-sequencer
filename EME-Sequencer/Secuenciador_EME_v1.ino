#include <EEPROMex.h>
#include <EEPROMVar.h>
#include <ResponsiveAnalogRead.h>
#include <TimerOne.h>
#include <Bounce2.h>
#include <elapsedMillis.h>
#include "caracteres.h"
#include <LiquidCrystalFast.h>

byte splash = true;

/* Secuenciador EME v1
 *  
 *  Escrito por Lucas Leal >> https://instagram.com/lucas.__.leal
 *  
 *  para el Laboratorio de Juguete
 *  https://github.com/labodejuguete/gamelin/blob/master/README.md
 *  https://laboratoriodejuguete.com/
 */
//caracteres especiales
#define CIRCULO_VACIO byte(0)
#define CIRCULO_LLENO 1
#define CIRCULO_VACIO_INV 2
#define CIRCULO_LLENO_INV 3
#define CIRCULO_VACIO_G 4
#define CIRCULO_LLENO_G 5

//mapeo botones
#define BOTON_IZQ 62
#define BOTON_DER 68
#define BOTON_ONOFF 56
#define BOTON_DOWN 67
#define BOTON_UP 63
#define BOTON_START 2
#define BOTON_SHIFT 57
#define BOTON_VOZ1 53
#define BOTON_VOZ2 51
#define BOTON_VOZ3 49
#define BOTON_VOZ4 47
#define CLOCK_INPUT 25
#define CLOCK_OUTPUT 37

#define VOICES 4 //cantidad de voces/paginas

//constantes modos
#define UP 0
#define DOWN 1
#define UPDOWN 2
#define RANDOM 3

#define SUBS 4 //cuantas subdivisiones vamos a usar

//constantes clock input
#define EXTERNAL_CLK 0
#define INTERNAL_CLK 1
#define DISCONNECTED 0
#define CONNECTED 1

LiquidCrystalFast lcd(10, 9, 8, 6, 5, 4, 3);
// RS RW EN  D4 D5 D6 D7

ResponsiveAnalogRead pot(A0, true);
Bounce botonIzq = Bounce();
Bounce botonDer = Bounce();
Bounce botonOnOff = Bounce();
Bounce botonUp = Bounce();
Bounce botonDown = Bounce();
Bounce botonStart = Bounce();
Bounce botonShift = Bounce();

byte pinesParte[VOICES] = {BOTON_VOZ1, BOTON_VOZ2, BOTON_VOZ3, BOTON_VOZ4};
Bounce botonesParte[VOICES];

int stepState[VOICES][16] = { //Array 2D, primer array cantidad de voces/paginas, segundo array cantidad de pasos
  {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

volatile byte reset[VOICES] = {16, 16, 16, 16};
volatile int currentStep[VOICES];
volatile byte  tickFlag[VOICES]; //esta variable la usamos como flag booleana para disparar los eventos sólo una vez fuera de la ISR.
volatile int  tickSub[VOICES] = {60, 60, 60, 60}; //tickSub almacena los valores de subdivisión para cada voz.
const volatile int tablaSub[SUBS] = {60, 120, 180, 240}; // valores fijos representando cada subdivision, X1, X2, X3, X4, X6?, X8?

volatile byte mode[VOICES] = {UP, UP, UP, UP}; //por defecto arranca con las 4 voces en modo UP
volatile byte mute[VOICES] = {1., 1., 1., 1.}; //con todas las salidas prendidas
volatile int gateTicks[VOICES] = {60, 60, 60, 60}; //en semicorcheas

volatile unsigned long tickCount;
volatile byte upDownFlag[VOICES];
volatile byte direccion[VOICES];
float gateMult[VOICES] = {1., 1., 1., 1.}; //multiplicador de gate, 100% *1., 75% *0.75, etc...
const byte outputs[VOICES] {33, 31, 29, 27};// aca poner las salidas digitales correspondientes
const byte ledsMute[VOICES] {45, 43, 41, 39}; //salidas de leds prendido/apagado

int page;
int cursor;
int modeSelektor;
int subSelektor[SUBS];
String subDisplay[SUBS] = {"/1", "/2", "/3", "/4"};
boolean shift = false;
volatile bool startInternal = true;
volatile bool startExternal = true;

//variables potenciometro, BPM y reset
float BPMclock[4] = {4166.668, 2083.334, 1388.887, 1041.667};
int BPMdisplay[4] = {60, 120, 180, 240};
byte BPMindex;
byte BPMindexLast;
volatile byte potReset[VOICES] = {16, 16, 16, 16};
volatile byte potResetLast[VOICES] = {16, 16, 16, 16};

//variables clock input
volatile bool clkSource = INTERNAL_CLK;
volatile  float pulsePeriod[VOICES];
volatile  float pulseStart[VOICES] = {micros(), micros(), micros(), micros()};
volatile  float pulseStop[VOICES] = {micros(), micros(), micros(), micros()};
volatile bool flagGateOn;
volatile bool flagGateOff;
volatile unsigned long count;
elapsedMicros gateMicros[VOICES];
volatile bool clkInput;
unsigned long disconnectedSince;
bool clkInState;
bool clkInStateLast;

//variables eeprom
byte eepromButton;
bool eepromButtonLast;
long eepromTimer;
volatile unsigned long tickCountExt;
void setup() {
  Serial.begin(115200);
  for (byte i = 0; i < VOICES; i++) {
    pinMode(outputs[i], OUTPUT);
    pinMode(ledsMute[i], OUTPUT);
  }
  pinMode(CLOCK_INPUT, INPUT_PULLUP);
  pinMode(CLOCK_OUTPUT, OUTPUT);

  lcd.begin(16, 2);
  //inicializamos botones
  botonDer.attach(BOTON_DER, INPUT_PULLUP);
  botonIzq.attach(BOTON_IZQ, INPUT_PULLUP);
  botonOnOff.attach(BOTON_ONOFF, INPUT_PULLUP);
  botonDown.attach(BOTON_DOWN, INPUT_PULLUP);
  botonUp.attach(BOTON_UP, INPUT_PULLUP);
  botonStart.attach(BOTON_START, INPUT_PULLUP);
  botonShift.attach(BOTON_SHIFT, INPUT_PULLUP);
  botonDer.interval(25);
  botonIzq.interval(25);
  botonOnOff.interval(25);
  botonDown.interval(25);
  botonUp.interval(25);
  botonStart.interval(25);
  botonShift.interval(25);
  eepromButton = digitalRead(BOTON_SHIFT);
  // Creamos carácteres especiales

  if (!eepromButton) { //si se prende manteniendo el boton de eeprom (shift) lee el preset en memoria
 
    for (int i = 0; i < VOICES; i++) { //esto se hizo muy rapido, la estructura de memoria podria ser mejor. Pero funciona y solo necesitamos 1 memoria.
      mode[i] = EEPROM.readByte(i);
      mute[i] = EEPROM.readByte(i + VOICES);
      reset[i] = EEPROM.readByte(i + (VOICES * 2));
      potReset[i] = EEPROM.readByte(i + (VOICES * 2));
      subSelektor[i] =  EEPROM.readByte(i + (VOICES * 3));
      tickSub[i] = tablaSub[subSelektor[i]];
      //Serial.println(EEPROM.readInt(i + (VOICES * 3)));
      gateMult[i] = EEPROM.readFloat((i * 4) + (VOICES * 4));
      gateTicks[i] = tickSub[i] * gateMult[i];
      BPMindex = EEPROM.readByte(i + (VOICES * 8)); //i*4 porque es float

    }
    for (int i = 0; i < 16; i++) {
      stepState[0][i] = EEPROM.readByte(100 + i);
      stepState[1][i] =  EEPROM.readByte(116 + i);
      stepState[2][i] =  EEPROM.readByte(132 + i);
      stepState[3][i] =  EEPROM.readByte(148 + i);
    }

  }

  for (int i = 0; i < VOICES; i++) {
    digitalWrite(ledsMute[i], mute[i]);
  }

  for (int i = 0; i < VOICES; i++) {
    botonesParte[i].attach(pinesParte[i], INPUT_PULLUP);
    botonesParte[i].interval(25);
  }
  if (splash) {
    //Splash
    lcd.createChar(0, eIzq);
    lcd.createChar(1, eme);
    lcd.createChar(2, eDer);
    //
    lcd.setCursor(0, 0);
    lcd.print("LABORATORIO");
    lcd.setCursor(0, 1);
    lcd.print("DE JUGUETE");
    delay(1000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write(0);
    lcd.setCursor(1, 0);
    lcd.write(1);
    lcd.setCursor(2, 0);
    lcd.write(2);

    for (int i = 0; i < 13; i++) {
      lcd.setCursor(i + 3, 0);
      lcd.print("*");
      delay(50);
    }
    for (int i = 0; i < 16; i++) {
      lcd.setCursor(i, 1);
      lcd.print("*");
      delay(50);
    }

    delay(600);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("HARDWARE:");
    lcd.setCursor(0, 1);
    lcd.print("Jorge Crowe");

    delay(1000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SOFTWARE:");
    lcd.setCursor(0, 1);
    lcd.print("Lucas Leal");

    delay(1000);
  }
  lcd.clear();
  lcd.createChar(0, circuloVacio);
  lcd.createChar(1, circuloLleno);
  lcd.createChar(2, circuloVacioInvertido);
  lcd.createChar(3, circuloLlenoInvertido);
  lcd.createChar(4, circuloVacioGuion);
  lcd.createChar(5, circuloLlenoGuion);
  lcd.createChar(6, simboloUpDown);
  lcd.createChar(7, unoInv);

  lcd.setCursor(page, 1); //seteamos cursor y escribimos el simbolo correspondiente al numero invertido (en la inicializacion es la voz 1)
  lcd.write(7); //escribmos el 7 que lo inicializamos como unoInv
  lcd.setCursor(1, 1);
  lcd.print(234); //escribimos el resto de las paginas sin invertir
  lcd.setCursor(13, 1);
  lcd.print(BPMdisplay[BPMindex]);
  refreshReset(reset[0]); //mostrar reset por primera vez...

  refreshMode(mode[page]); //mostrar el modo por primera vez...
  refreshSub(subSelektor[page]); //mostrar la subdivision por primera vez...

  //... y el estado de los 16 pasos
  for (int i = 0; i < 16; i++) {
    lcd.setCursor(i, 0);
    if (stepState[page][i]) {
      lcd.write(CIRCULO_LLENO);
    }
    else {
      lcd.write(CIRCULO_VACIO);
    }
  }
  Timer1.initialize(BPMclock[BPMindex]);
  //Timer1.initialize(1041.667 * 2); //120 BPM = 500mS / 480 ticks = 1041.667uS. * 2 la negra es 240
  Timer1.attachInterrupt(clk);

}
/////////FIN SETUP

/////////ISR

//La parte importante del sequencer está en la interrupción generada por el timer clk(). El timer genera pulsos (ticks)
// a una velocidad mucho mas alta que el tempo. Hacemos nuestra propia relación de ppqn. Tomamos que 240 ticks son una negra.
//En la función clk(), tickCount incrementa 1 en cada tick, entonces chequeamos: si pasaron 240 ticks, esto es una negra, 120 ticks
//corchea, etc.

void clk() {
  static unsigned long tickResult[VOICES];

  static unsigned long tickCountGate[VOICES];
  static unsigned long gateFlag[VOICES];
  static unsigned long tickResultLast[VOICES];
  if (startInternal && clkSource == INTERNAL_CLK) {
    //tickRelativo = tickCount / 10; //10 ticks para midi clock, 240 para negra, 120 corchea, etc.
    if (tickCount % 10 == 0) {
      // midi clock
    }
    if (tickCount % 60 == 0) { //60 == semi para clock analogico out

      digitalWrite(CLOCK_OUTPUT, HIGH);
    }
    if (tickCount % 60 == 5) { //un poco de gate

      digitalWrite(CLOCK_OUTPUT, LOW);
    }

    //tickResult[i] almacena directamente el valor del paso dado por la formula...
    //sin tener que hacer otro calculo. Divide tickCount por la subdivisión deseada
    // y nos quedamos con el resto de la división  del reset.
    // En esta fórmula tenemos entonces la info de sub y de reset individual.
    for (int i = 0; i < VOICES; i++) { //repetimos el proceso para las 4 voces.

      tickResult[i] = (tickCount / tickSub[i]) % reset[i];

      if (tickResult[i] != tickResultLast[i]) { //Queremos que los eventos se disparen sólo cuando cambiò el resultado
        if (tickResult[i] == 0) { //cada vez que toque el 0 va a alternar entre up/down
          upDownFlag[i] = !upDownFlag[i];
          if (upDownFlag[i] == 1) { //para que sea mas claro...
            direccion[i] = UP; //cuenta para arriba, variable que va al switch
          }
          else {
            direccion[i] = DOWN; //cuenta para abajo
          }

        }
        switch (mode[i]) {
          case UP:
            currentStep[i] = tickResult[i]; //tickResult marca el número de pasos de forma incremental
            break;
          case DOWN:
            currentStep[i] = (reset[i] - 1) - tickResult[i]; // entonces si queremos invertir el rango, hacemos max - input. En este caso max = reset-1, input = tickResult
            break;
          case UPDOWN:
            if (direccion[i] == UP) { //si va para arriba el currentStep es igual a tickResult (up)
              currentStep[i] = tickResult[i]; //tickResult marca el número de pasos de forma incremental
            }
            else { //si cuenta para abajo, invierte el resultado haciendo MAX(reset-1) - input(tickResult).
              currentStep[i] = (reset[i] - 1) - tickResult[i];
            }
            break;
          case RANDOM:
            currentStep[i] = random(0, reset[i]);
            break;
        }
        if (stepState[i][currentStep[i]]) { //prendemos leds evaluando si el paso actual esta prendido
          tickCountGate[i] = tickCount; // tickCountGate almaacena la cuenta de tickCount para calcular el tiempo de gate

          if (mute[i]) {
            digitalWrite(outputs[i], HIGH);
          }
          gateFlag[i] = true; //flag para apagar el gate mas abajo una sola vez

        }


        tickFlag[i] = true; //esta variable va al loop y se usa para ejecutar la función de display.

      }



      if (tickCount - tickCountGate[i] > gateTicks[i] && gateFlag[i] == true) { // si tickCount aumentó X, apagamos la salida digital

        if (mute[i]) {
          digitalWrite(outputs[i], LOW);

        }
        gateFlag[i] = false;

        tickCountGate[i] = tickCount;

      }
      tickResultLast[i] = tickResult[i];
    }
 
    tickCount++; //tickCount cuenta sin parar a la velocidad de la interrupción del timer.
  }
}
volatile unsigned long gateFlagExt[VOICES];
void clkIn() {

  static unsigned long tickResult[VOICES];

  static unsigned long tickCountGate[VOICES];

  static unsigned long tickResultLast[VOICES];

  if (digitalRead(CLOCK_INPUT) == LOW) {
    if (startExternal) {
      count++; // count lo usamos para calcular el periodo del pulso. Mas abajo hace modulo % 2 para calcular el periodo entre count 0 y 1, 2 y 3, 4 y 5, etc.
      digitalWrite(CLOCK_OUTPUT, LOW);

      for (int i = 0; i < VOICES; i++) { //repetimos el proceso para las 4 voces.

        tickResult[i] = (tickCountExt / (tickSub[i] / 60)) % reset[i];

        if (tickResult[i] != tickResultLast[i]) { //Queremos que los eventos se disparen sólo cuando cambiò el resultado


          if (tickResult[i] == 0) { //cada vez que toque el 0 va a alternar entre up/down
            upDownFlag[i] = !upDownFlag[i];
            if (upDownFlag[i] == 1) { //para que sea mas claro...
              direccion[i] = UP; //cuenta para arriba, variable que va al switch
            }
            else {
              direccion[i] = DOWN; //cuenta para abajo
            }

          }
          switch (mode[i]) {
            case UP:
              currentStep[i] = tickResult[i]; //tickResult marca el número de pasos de forma incremental
              break;
            case DOWN:
              currentStep[i] = (reset[i] - 1) - tickResult[i]; // entonces si queremos invertir el rango, hacemos max - input. En este caso max = reset-1, input = tickResult
              break;
            case UPDOWN:
              if (direccion[i] == UP) { //si va para arriba el currentStep es igual a tickResult (up)
                currentStep[i] = tickResult[i]; //tickResult marca el número de pasos de forma incremental
              }
              else { //si cuenta para abajo, invierte el resultado haciendo MAX(reset-1) - input(tickResult).
                currentStep[i] = (reset[i] - 1) - tickResult[i];
              }
              break;
            case RANDOM:
              currentStep[i] = random(0, reset[i]);
              break;
          }

          if (stepState[i][currentStep[i]]) { //prendemos leds evaluando si el paso actual esta prendido

            if (mute[i]) {
              gateMicros[i] = 0;
              digitalWrite(outputs[i], HIGH);

            }
            gateFlagExt[i] = true; //flag para apagar el gate mas abajo una sola vez

          }
          else {
            // digitalWrite(outputs[i], LOW); //forzamos apagado

          }
          tickFlag[i] = true; //esta variable va al loop y se usa para ejecutar la función de display.
        }


        tickResultLast[i] = tickResult[i];
        if (count % 2 == 0) {

          pulseStart[i] = micros(); //empieza a contar para calcular el periodo del pulso, ademas lo vamos a usar
          //para calcular el tiempo de gate interno

        }

        else if (count % 2 == 1) { //se completo un pulso

          pulseStop[i] = micros();
          //medimos el pulso del clock in, pero multiplicamos el periodo por 1, 2, 3 o 4 segun la subdivision para que el tiempo de gate se ajuste automaticamente
          pulsePeriod[i] = abs((pulseStop[i] - pulseStart[i]) * (tickSub[i] / 60));
        }
      }
      tickCountExt++;
    }

  }
  else if (digitalRead(CLOCK_INPUT) == HIGH) {
    digitalWrite(CLOCK_OUTPUT, HIGH);
  }

}


void loop() {

  checkClockSource();

  //REFRESCAR PASOS DEL DISPLAY
  for (byte i = 0; i < VOICES; i++) {
    if ( tickFlag[i] ) {
      //las cosas que demoran tiempo las mandamos al loop
      if (i == page) { //que ejecute la función de display sólo en la voz activa
        displayPaso(currentStep[page]);
      }
      tickFlag[i]  = false;
    }
    if (clkSource == EXTERNAL_CLK && gateFlagExt[i]) { //apaga el paso segun el tiempo de gate
      if (gateMicros[i] >= pulsePeriod[i] * (gateMult[i] * 1.)) {
        if (mute[i]) {
          digitalWrite(outputs[i], LOW);
        }
        gateFlagExt[i] = false;
      }
    }
  }

  //LECTURA DE POTENCIOMETRO
  pot.update();
  pot.getValue();
  if (pot.hasChanged()) {

    if (!shift) { //agregamos condicional aca para que no actualice la variable cuando se aprieta o suelta shift
      BPMindex = pot.getValue() / 256; //queremos solo 4 valores para usar como indice de lectura del array de BPMs
    }
    else {

      potReset[page] = (pot.getValue() / 64) + 1;
      refreshReset(potReset[page]); //lo refrescamos aca para el display pero el valor real se refresca cuando empieza la secuencia
    }
    eepromTimer = 0;
  }
  if (BPMindex != BPMindexLast) {
    BPMindexLast = BPMindex;

    //seteamos velocidad de la interrupción del master clock
    Timer1.setPeriod(BPMclock[BPMindex]);
    if (clkSource == INTERNAL_CLK) { //refresca BPM solo si el clock es interno
      lcd.setCursor(13, 1);
      lcd.print(BPMdisplay[BPMindex]);
      if (BPMindex == 0) { //si el bpm es 60 borrame el 0 de sobra a la derecha
        lcd.setCursor(15, 1);
        lcd.print(" ");
      }
    }
  }
  /////////
  for (int i = 0; i < VOICES; i++) {

    if (potReset[i] != potResetLast[i]) { // actualizamos el reset cuando vuelve a empezar la secuencia
      if (mode[i] != RANDOM) {
        if (currentStep[i] == 0) {
          potResetLast[i] = potReset[i];
          reset[i] = potReset[i];
        }
      }
      else {
        potResetLast[i] = potReset[i];
        reset[i] = potReset[i];
      }
    }
  }

  //LECTURA DE BOTONES
  botonDer.update();
  botonIzq.update();
  botonOnOff.update();
  botonDown.update();
  botonUp.update();
  botonStart.update();
  botonShift.update();
  EEPROMfunc();
  for (int i = 0; i < VOICES; i++) {
    botonesParte[i].update();
  }

  ///////// BOTONES < > /////////////////
  if (botonDer.fell() || botonIzq.fell()) {
    eepromTimer = 0;
    //////DERECHA
    if (botonDer.fell()) {
      if (!shift  && cursor < 15) {
        cursor++;
        lcd.setCursor(cursor, 0);
        if (stepState[page][cursor]) {
          lcd.write(CIRCULO_LLENO_G);////agrego guion, mas facil
        }
        else {
          lcd.write(CIRCULO_VACIO_G); ////agrego guion, mas facil
        }
        ////////
        lcd.setCursor(cursor - 1, 0);
        if (stepState[page][cursor - 1]) {
          lcd.write(CIRCULO_LLENO);
        }
        else {
          lcd.write(CIRCULO_VACIO);
        }
      }
      else if (shift) {
        if (subSelektor[page] < 3) {
          subSelektor[page]++; //lo modifica
        }
        tickSub[page] = tablaSub[subSelektor[page]]; // lo asigna al array de modo segun la pagina activa, tickSub va a la interrupcion
        refreshSub(subSelektor[page]);
      }
    }

    ///////IZQUIERDA
    if (botonIzq.fell()) {

      if (!shift && cursor > 0) {
        cursor--;
        lcd.setCursor(cursor, 0);
        if (stepState[page][cursor]) {
          lcd.write(CIRCULO_LLENO_G); ////agrego guion, mas facil
        }
        else {
          lcd.write(CIRCULO_VACIO_G); //agrego guion, mas facil
        }
        ////////
        lcd.setCursor(cursor + 1, 0);
        if (stepState[page][cursor + 1]) {
          lcd.write(CIRCULO_LLENO);
        }
        else {
          lcd.write(CIRCULO_VACIO);
        }

      }
      else if (shift) {
        if (subSelektor[page] > 0) {
          subSelektor[page]--; //lo modifica
        }
        tickSub[page] = tablaSub[subSelektor[page]];
        refreshSub(subSelektor[page]);
      }
    }

  }
  ///////// BOTON switch on/off /////////////////
  if (botonOnOff.fell()) {
    eepromTimer = 0;
    if (!shift) { //si no esta en shift prende/apaga paso
      stepState[page][cursor] = !stepState[page][cursor];
      lcd.setCursor(cursor, 0);
      if (stepState[page][cursor]) {

        lcd.write(CIRCULO_LLENO_G);////agrego guion, mas facil
      }
      else {
        lcd.write(CIRCULO_VACIO_G); ////agrego guion, mas facil
      }

    }
    else { //si esta en shift, start/stop
      if (clkSource == INTERNAL_CLK) {
        startInternal = !startInternal;
        if (startInternal) {
          lcd.setCursor(11, 1);
          lcd.print("S");
          Timer1.start();

          tickCount = 0; //reseteamos fase just in case
        }
        else {
          Timer1.stop();
          tickCount = 0; //reseteamos fase
          lcd.setCursor(11, 1);
          lcd.print("P");
          for (int i = 0; i < VOICES; i++) {
            digitalWrite(outputs[i], LOW);

          }

        }
      }
      else if (clkSource == EXTERNAL_CLK) {

        startExternal = !startExternal;
        if (startExternal) {
          tickCountExt = 0;
          lcd.setCursor(11, 1);
          lcd.print("S");
        }
        else {
          tickCountExt = 0;
          lcd.setCursor(11, 1);
          lcd.print("P");
          for (int i = 0; i < VOICES; i++) {
            digitalWrite(outputs[i], LOW);

          }
        }

      }
    }
  }

  ///////// BOTONES pgUp pgDwn /////////////////
  if (botonDown.fell() ) {
    eepromTimer = 0;
    if (!shift) {//AHORA ES TIEMPO DE GATE
      gateMult[page] -= 0.25;
      if (gateMult[page] <= 0.25) {
        gateMult[page] = 0.25;
      }
      gateTicks[page] = tickSub[page] * gateMult[page]; // calculamos el tiempo de gate multiplicando el tickSub actual por gateMult. gateTicks se va al ISR.
      refrescarGate(gateMult[page] * 100); //pasamos de 0-1 a 0-100%
    }
    else if (shift) {

      modeSelektor = mode[page]; //para que vuelva al mismo numero
      if (modeSelektor > 0) {
        modeSelektor--; //lo modifica
      }
      mode[page] = modeSelektor; // lo asigna al array de modo segun la pagina activa
      refreshMode(modeSelektor);

    }
  }

  else if (botonUp.fell()) {
    eepromTimer = 0;
    if (!shift) {//AHORA ES TIEMPO DE GATE
      gateMult[page] += 0.25;
      if (gateMult[page] >= 1.) {
        gateMult[page] = 1.;
      }
      gateTicks[page] = tickSub[page] * gateMult[page]; // calculamos el tiempo de gate multiplicando el tickSub actual por gateMult. gateTicks se va al ISR.
      refrescarGate(gateMult[page] * 100); //pasamos de 0-1 a 0-100%
    }
    else if (shift) {

      modeSelektor = mode[page]; //para que vuelva al mismo numero
      if (modeSelektor < 3) {
        modeSelektor++; //lo modifica
      }
      mode[page] = modeSelektor; // lo asigna al array de modo segun la pagina activa
      refreshMode(modeSelektor);
    }
  }

  ///////// BOTON start /////////////////
  if (botonStart.fell()) { // NO HAY

  }

  ///////// BOTON shift /////////////////
  if (botonShift.fell()) {

    shift = true;
  }
  else if (botonShift.rose()) {

    shift = false;
  }
  ///////// BOTONES VOCES 1 2 3 4 /////////////////

  for (int i = 0; i < VOICES; i++) {
    if (botonesParte[i].fell()) {
      eepromTimer = 0;

      if (!shift) {
        page = i;
        refrescarPagina(page);
      }
      else {
        mute[i] = !mute[i];

        digitalWrite(ledsMute[i], mute[i]);
        if (mute[i] == 0) {
          digitalWrite(outputs[i], LOW);
        }
      }
    }

  }

}


void refreshMode(byte mode) {
  switch (mode) {
    case 0: lcd.createChar(6, simboloUp);
      break;
    case 1: lcd.createChar(6, simboloDown);
      break;
    case 2: lcd.createChar(6, simboloUpDown);
      break;
    case 3: lcd.createChar(6, simboloRandom);
      break;
  }
  lcd.setCursor(7, 1);
  lcd.write(6);
}

void refreshSub(int sub) {
  lcd.setCursor(5, 1);
  lcd.print(subDisplay[sub]);
}

void refreshReset(int rst) {
  lcd.setCursor(9, 1);
  lcd.print(rst);
  if (rst < 10) {
    lcd.setCursor(10, 1);
    lcd.print(" "); //borra el 0 de sobra...
  }
}

void refrescarGate(int porcentaje) {
  lcd.setCursor(13, 1);
  lcd.print(porcentaje);
  if (porcentaje < 100) {
    lcd.setCursor(15, 1);
    lcd.print(" ");
  }
}

void refrescarPagina(byte pg) { //refrescamos todos los parametros cuando se cambia de pagina (gate, modo, sub, reset, etc)
  unsigned long startTime = micros();
  static byte pgLast; //static crea la variable localmente pero no la destruye
  //pgLast la usamos para comparar el estado de los pasos de la pagina
  //actual con la anterior así sólo refrescamos en el display si el paso (prendido o apagado) es
  //distinto. Así ahorramos recursos.
  for (int i = 0; i < 16; i++) { //escaneamos los 16 pasos según la página

    if (stepState[pg][i] != stepState[pgLast][i]) {   //escribimos al display sólo si el paso es distinto al de la pagina anterior.
      lcd.setCursor(i, 0);
      if (stepState[pg][i]) { //si el paso esta prendido escribimos el circulo lleno
        lcd.write(CIRCULO_LLENO);

      }
      else {
        lcd.write(CIRCULO_VACIO); // si no, el vacío

      }
    }

  }
  if (mode[pg] != mode[pgLast]) { //escribimos al display sólo si el modo es distinto al de la pagina anterior.
    refreshMode(mode[pg]);
  }
  if (tickSub[pg] != tickSub[pgLast]) {
    refreshSub(subSelektor[page]);
  }
  if (potReset[pg] != potReset[pgLast]) {
    refreshReset(potReset[page]);
  }

  refrescarGate(gateMult[pg] * 100);
  lcd.setCursor(cursor, 0);
  if (stepState[pg][cursor]) {
    lcd.write(CIRCULO_LLENO_G);////agrego guion, mas facil
  }
  else {
    lcd.write(CIRCULO_VACIO_G); ////agrego guion, mas facil
  }
  unsigned long endTime = micros();

  //////MOSTRAR NUMERO PAGINA Y SIMBOLO

  switch (pg) {
    case 0:
      lcd.createChar(7, unoInv);
      break;
    case 1:
      lcd.createChar(7, dosInv);
      break;
    case 2:
      lcd.createChar(7, tresInv);
      break;
    case 3:
      lcd.createChar(7, cuatroInv);
      break;
  }
  lcd.setCursor(pgLast, 1);
  lcd.print(pgLast + 1);
  lcd.setCursor(pg, 1);
  lcd.write(7);


  pgLast = pg; //igualamos pgAnterior a pg para la próxima vuelta
}


void displayPaso(int paso) {

  static int pasoAnterior;
  lcd.setCursor(paso, 0);
  if (stepState[page][paso]) {
    lcd.write(CIRCULO_LLENO_INV);
  }
  else {
    lcd.write(CIRCULO_VACIO_INV);
  }
  lcd.setCursor(pasoAnterior, 0);
  if (stepState[page][pasoAnterior]) {
    lcd.write(CIRCULO_LLENO);
  }
  else {
    lcd.write(CIRCULO_VACIO);
  }

  if (cursor != pasoAnterior) {
    lcd.setCursor(pasoAnterior, 0);
    if (stepState[page][pasoAnterior]) {
      lcd.write(CIRCULO_LLENO);
    }
    else {
      lcd.write(CIRCULO_VACIO);
    }
  }
  else {
    lcd.setCursor(pasoAnterior, 0);
    if (stepState[page][pasoAnterior]) {
      lcd.write(CIRCULO_LLENO_G);

    }
    else {
      lcd.write(CIRCULO_VACIO_G);
    }
  }
  pasoAnterior = paso;

}

void checkClockSource() {
  clkInState = digitalRead(CLOCK_INPUT);
  if (clkInState != clkInStateLast) {

    clkIn(); //funcion de clk input. Es casi igual a la ISR del clock interno
    clkInStateLast = clkInState;
  }
  if (clkInState) {
    disconnectedSince++;
  }

  else {

    disconnectedSince = 0;
    if (clkInput == CONNECTED) { //si se conecto el cable
      Serial.println("CONECTADO");
      lcd.setCursor(13, 1);
      lcd.print("EXT");
      if (startExternal) {
        lcd.setCursor(11, 1);
        lcd.print("S");
      }
      else {
        lcd.setCursor(11, 1);
        lcd.print("P");
      }

      clkInput = 0; //flag para pasar al estado desconectado
      clkSource = EXTERNAL_CLK;
    }

  }

  if (disconnectedSince > 600 && clkInput == DISCONNECTED) { // si estuvo desconectado un ratito vuelve a clock interno

    lcd.setCursor(13, 1);
    lcd.print(BPMdisplay[BPMindex]);

    if (BPMindex == 0) { //si el bpm es 60 borrame el 0 de sobra a la derecha
      lcd.setCursor(15, 1);
      lcd.print(" ");
    }
    if (startInternal) {
      lcd.setCursor(11, 1);
      lcd.print("S");
    }
    else {
      lcd.setCursor(11, 1);
      lcd.print("P");
    }
    Serial.println("DESCONECTADO");
    clkInput = 1;
    clkSource = INTERNAL_CLK;
  }
}

byte shiftCounter;
void EEPROMfunc() {
  eepromButton = digitalRead(BOTON_SHIFT);
  if (eepromButton != eepromButtonLast) {
    eepromTimer = 0;
    lcd.setCursor(12, 1);
    lcd.print(" ");
    eepromButtonLast = eepromButtonLast;
  }

  if (!eepromButton) { //pull up, al reves
    eepromTimer++;
  }

  if (eepromTimer > 4000) {

    for (int i = 0; i < VOICES; i++) {
      EEPROM.updateByte(i, mode[i]);
      EEPROM.updateByte(i + VOICES, mute[i]);
      EEPROM.updateByte(i + (VOICES * 2), reset[i]);
      // EEPROM.updateInt((i * 2) + (VOICES * 3), tickSub[i]); // i*2 porque es entero
      EEPROM.updateByte(i + (VOICES * 3), subSelektor[i]);
      EEPROM.updateFloat((i * 4) + (VOICES * 4), gateMult[i]); //i*4 porque es float
      EEPROM.updateByte(i + (VOICES * 8), BPMindex); //i*4 porque es float

      Serial.println("SAVED");
      lcd.setCursor(12, 1);
      lcd.print("*");
      eepromTimer = 0;
    }
    for (int i = 0; i < 16; i++) {
      EEPROM.updateByte(100 + i, stepState[0][i]);
      EEPROM.updateByte(116 + i, stepState[1][i]);
      EEPROM.updateByte(132 + i, stepState[2][i]);
      EEPROM.updateByte(148 + i, stepState[3][i]);
    }
  }
}
