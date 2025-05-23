#include <driver/i2s_std.h>
#include <math.h>

// Configuración de I2S
#define SAMPLE_RATE 44100 // Frecuencia de muestreo (44.1 kHz)
#define SAMPLE_BUFFER_SIZE 64 // Tamaño del buffer de muestras
#define MAX_VOICES 8 // Número máximo de voces (polifonía)
#define NUM_INSTRUMENTS 12 // Número de instrumentos disponibles

// Estados de la envolvente ADSR
#define ADSR_IDLE    0 // Inactivo
#define ADSR_ATTACK  1 // Ataque
#define ADSR_DECAY   2 // Decaimiento
#define ADSR_SUSTAIN 3 // Sostenimiento
#define ADSR_RELEASE 4 // Liberación

// Tabla de seno precalculada (2048 puntos, 0 a 2π)
#define SINE_TABLE_SIZE 2048
static float sineTable[SINE_TABLE_SIZE];
#define SINE_SCALE ((float)SINE_TABLE_SIZE / TWO_PI)

// Macro para reemplazar constrain
#define enRango(v, rI, rS) ((v > rS) ? rS : ((v < rI) ? rI : v))

// Variables globales
TaskHandle_t _synthTaskHandle; // Handle para la tarea de síntesis
i2s_chan_handle_t _i2sConfigHand; // Handle para la configuración de I2S
uint32_t sampleBuf[SAMPLE_BUFFER_SIZE]; // Buffer de muestras de audio
uint8_t voiceInstrument[MAX_VOICES]; // Almacena el instrumento asignado a cada voz
static float globalVolume = 1.0f; // Volumen global del sintetizador (0.0 a 1.0)

// Constantes precalculadas
static const float TIME_STEP = 1.0f / SAMPLE_RATE; // Paso de tiempo por muestra

// Estructura para una voz FM
typedef struct {
    float carrierFreq = 0.0f; // Frecuencia de la portadora
    float modulatorFreq = 0.0f; // Frecuencia del modulador
    float modulationIndex = 0.0f; // Índice de modulación
    float amplitude = 0.0f; // Amplitud de la voz
    float phase = 0.0f; // Fase de la portadora
    float modPhase = 0.0f; // Fase del modulador
    float currentLevel = 0.0f; // Nivel actual de la envolvente ADSR
    uint8_t state = ADSR_IDLE; // Estado de la envolvente ADSR
    bool active = false; // Indica si la voz está activa
    float timeElapsed = 0.0f; // Tiempo transcurrido desde el inicio de la nota
    uint8_t note = 0; // Nota MIDI asignada
    uint8_t channel = 0; // Canal asignado
} FmVoice;

// Estructura para un instrumento
typedef struct {
    float loudness; // Volumen base del instrumento
    float pitchOffset; // Desplazamiento de tono (en semitonos)
    float attack; // Tiempo de ataque (segundos)
    float decay; // Tiempo de decaimiento (segundos)
    float sustain; // Nivel de sostenimiento (0.0 a 1.0)
    float release; // Tiempo de liberación (segundos)
    float fmFreqMultiplier; // Multiplicador de frecuencia para FM
    float fmAmpStart; // Amplitud inicial del modulador
    float fmAmpEnd; // Amplitud final del modulador
    float fmDecay; // Decaimiento del modulador
} Instrument;

// Tabla de instrumentos predefinida
const Instrument instruments[NUM_INSTRUMENTS] = {
    // loudness, pitch, attack, decay, sustain, release, fmFreq, fmAmpS, fmAmpE, fmDec
    {57.6, 0, 0.05f, 0.3f, 0.6f, 0.5f, 256, 128, 51.2, 102.4},    // Piano
    {51.2, 12, 0.01f, 0.15f, 0.0f, 0.2f, 768, 512, 128, 25.6},   // Xilófono
    {44.8, 0, 0.03f, 0.4f, 0.5f, 0.6f, 384, 256, 76.8, 128},     // Guitarra
    {38.4, 24, 0.001f, 0.8f, 0.0f, 0.4f, 1280, 1024, 256, 179.2}, // Platillo
    {51.2, 12, 0.02f, 0.9f, 0.0f, 0.3f, 640, 384, 51.2, 204.8},   // Campana
    {57.6, 0, 0.03f, 0.2f, 0.7f, 0.3f, 128, 768, 256, 51.2},     // Funky
    {44.8, 12, 0.1f, 0.3f, 0.6f, 0.4f, 512, 307.2, 102.4, 153.6}, // Vibráfono
    {38.4, 24, 0.01f, 1.0f, 0.0f, 0.5f, 1792, 1280, 256, 230.4},  // Gong
    {51.2, 0, 0.2f, 0.1f, 0.9f, 0.6f, 256, 76.8, 25.6, 76.8},     // Violín
    {64, -12, 0.05f, 0.2f, 0.8f, 0.4f, 128, 128, 51.2, 76.8},    // Bajo
    {57.6, 0, 0.08f, 0.2f, 0.8f, 0.3f, 307.2, 256, 128, 51.2},   // Trompeta
    {51.2, 0, 0.04f, 0.2f, 0.7f, 0.3f, 384, 204.8, 76.8, 102.4}  // Armónica
};

FmVoice voices[MAX_VOICES];

// Convierte una nota MIDI a frecuencia
static inline float IRAM_ATTR midi2Freq(uint8_t midiNote) {
    return 8.17579891f * expf(0.05776226f * midiNote);
}

// Inicializa la tabla de seno
void initSineTable() {
    // Genera una tabla de seno con 2048 puntos para optimizar cálculos FM
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        sineTable[i] = sinf(i * TWO_PI / SINE_TABLE_SIZE);
    }
}

// Actualiza la envolvente ADSR para una voz
[[gnu::optimize("fast-math")]]
static inline void IRAM_ATTR updateADSR(FmVoice& voice, const Instrument& instr, float timeStep) {
    switch (voice.state) {
        case ADSR_ATTACK:
            voice.currentLevel += timeStep / instr.attack;
            if (voice.currentLevel >= 1.0f) {
                voice.currentLevel = 1.0f;
                voice.state = ADSR_DECAY;
            }
            break;
        case ADSR_DECAY:
            voice.currentLevel -= timeStep / instr.decay * (1.0f - instr.sustain);
            if (voice.currentLevel <= instr.sustain) {
                voice.currentLevel = instr.sustain;
                if (instr.sustain == 0.0f) {
                    voice.state = ADSR_RELEASE; // Pasa a RELEASE si no hay sustain
                } else {
                    voice.state = ADSR_SUSTAIN;
                }
            }
            break;
        case ADSR_RELEASE:
            voice.currentLevel -= timeStep / instr.release;
            if (voice.currentLevel <= 0.0f) {
                voice.currentLevel = 0.0f;
                voice.state = ADSR_IDLE;
                voice.active = false;
            }
            break;
        case ADSR_SUSTAIN:
            voice.currentLevel = instr.sustain; // Mantiene el nivel de sustain
            voice.timeElapsed += timeStep;
            break;
    }
}

// Establece el número máximo de voces
bool setMaxNotes(uint8_t maxNotas) {
    // Limita el número de voces al máximo permitido
    maxNotas = enRango(maxNotas, 1, MAX_VOICES);
    // Nota: En este código, MAX_VOICES es fijo, pero se podría redimensionar voices[] dinámicamente
    Serial.println("Máximo de notas establecido: " + String(maxNotas));
    return true;
}

// Establece el volumen global del sintetizador
void setVolume(uint8_t vol) {
    // Mapea el volumen de 0-100 a 0.0-1.0
    globalVolume = enRango(vol, 0, 100) / 100.0f;
    Serial.println("Volumen global establecido: " + String(globalVolume));
}

// Apaga todas las notas activas
void allNoteOff() {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active) {
            voices[i].active = false;
            voices[i].state = ADSR_IDLE;
            voices[i].currentLevel = 0.0f;
            voices[i].timeElapsed = 0.0f;
            voices[i].note = 0;
            voices[i].channel = 0;
        }
    }
    Serial.println("Todas las notas apagadas");
}

// Modifica el volumen de las notas en un canal
void alterVolNote(uint8_t channel, uint8_t velocity) {
    if (channel >= MAX_VOICES) return;
    float vol = velocity / 127.0f; // Mapea velocity a 0.0-1.0
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].channel == channel) {
            voices[i].amplitude = instruments[voiceInstrument[channel]].loudness * vol / 64.0f;
            Serial.println("Volumen de canal " + String(channel) + " ajustado a: " + String(vol));
            break;
        }
    }
}

// Modifica el tono de las notas en un canal
void alterPitchNote(uint8_t channel, uint8_t cant) {
    if (channel >= MAX_VOICES) return;
    float pitchOffset = map(cant, 0, 127, -16, 17); // Mapea a ±16 semitonos
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].channel == channel) {
            voices[i].carrierFreq = midi2Freq(voices[i].note + instruments[voiceInstrument[channel]].pitchOffset + pitchOffset);
            voices[i].modulatorFreq = voices[i].carrierFreq * instruments[voiceInstrument[channel]].fmFreqMultiplier / 256.0f;
            Serial.println("Tono de canal " + String(channel) + " ajustado con offset: " + String(pitchOffset));
            break;
        }
    }
}

// Asigna un instrumento a un canal
void setInstrument(uint8_t channel, uint8_t instrument) {
    if (channel < MAX_VOICES && instrument < NUM_INSTRUMENTS) {
        voiceInstrument[channel] = instrument;
        Serial.print("Asignado instrumento ");
        Serial.print(instrument);
        Serial.print(" al canal ");
        Serial.println(channel);
    }
}

// Busca una voz libre para una nueva nota
int findFreeVoice() {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            return i;
        }
    }
    // Si no hay voces libres, roba la más antigua
    int oldestVoice = 0;
    float oldestTime = voices[0].timeElapsed;
    for (int i = 1; i < MAX_VOICES; i++) {
        if (voices[i].timeElapsed > oldestTime) {
            oldestTime = voices[i].timeElapsed;
            oldestVoice = i;
        }
    }
    return oldestVoice;
}

// Activa una nota
void noteOn(uint8_t channel, uint8_t note, uint8_t velocity=127) {
    if (channel >= MAX_VOICES) return;

    // Busca una voz libre
    int voiceIdx = findFreeVoice();
    uint8_t instrumentIndex = voiceInstrument[channel];
    const Instrument& instr = instruments[instrumentIndex];
    float frequency = midi2Freq(note + instr.pitchOffset);

    // Configura los parámetros de la voz
    voices[voiceIdx].carrierFreq = frequency;
    voices[voiceIdx].modulatorFreq = frequency * instr.fmFreqMultiplier / 256.0f;
    voices[voiceIdx].modulationIndex = instr.fmAmpStart + (instr.fmAmpEnd - instr.fmAmpStart) * (velocity / 127.0f);
    voices[voiceIdx].amplitude = instr.loudness * (velocity / 127.0f) / 64.0f;
    voices[voiceIdx].phase = 0.0f;
    voices[voiceIdx].modPhase = 0.0f;
    voices[voiceIdx].currentLevel = 0.0f;
    voices[voiceIdx].state = ADSR_ATTACK;
    voices[voiceIdx].active = true;
    voices[voiceIdx].timeElapsed = 0.0f;
    voices[voiceIdx].note = note;
    voices[voiceIdx].channel = channel;
    Serial.print("Tocado en canal ");
    Serial.print(channel);
    Serial.print(" con instrumento ");
    Serial.println(instrumentIndex);
    Serial.print("Índice de voz ");
    Serial.println(voiceIdx);
}

// Apaga una nota
void noteOff(uint8_t channel, uint8_t note) {
    if (channel >= MAX_VOICES) return;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].channel == channel && voices[i].note == note) {
            // Solo aplica RELEASE si el instrumento tiene sustain
            if (instruments[voiceInstrument[voices[i].channel]].sustain > 0.0f) {
                voices[i].state = ADSR_RELEASE;
            }
        }
    }
}

// Genera una muestra FM
[[gnu::optimize("fast-math")]]
static inline int16_t IRAM_ATTR generateSample() {
    float mix = 0.0f;

    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active) {
            // Actualiza la envolvente ADSR para la voz
            updateADSR(voices[i], instruments[voiceInstrument[voices[i].channel]], TIME_STEP);

            // Actualiza la fase del modulador
            voices[i].modPhase += TWO_PI * voices[i].modulatorFreq * TIME_STEP;
            voices[i].modPhase = fmodf(voices[i].modPhase, TWO_PI);
            float modSignal = voices[i].modulationIndex * sineTable[(int)(voices[i].modPhase * SINE_SCALE) & (SINE_TABLE_SIZE - 1)];

            // Actualiza la fase de la portadora
            voices[i].phase += TWO_PI * (voices[i].carrierFreq + modSignal) * TIME_STEP;
            voices[i].phase = fmodf(voices[i].phase, TWO_PI);

            // Calcula la muestra actual para la voz, aplicando volumen global
            mix += voices[i].amplitude * voices[i].currentLevel * sineTable[(int)(voices[i].phase * SINE_SCALE) & (SINE_TABLE_SIZE - 1)] * globalVolume;
        }
    }

    // Normaliza y convierte a entero de 16 bits
    return (int16_t)(enRango(mix, -1.0f, 1.0f) * 32767);
}

// Bucle de síntesis
void synthProcess(void* parameter) {
    size_t bytesWritten;

    while (true) {
        // Genera un buffer de muestras
        for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
            int16_t sample = generateSample();
            sampleBuf[i] = ((uint32_t)sample << 16) | (uint16_t)sample; // Formato estéreo
        }
        // Escribe el buffer al canal I2S
        i2s_channel_write(_i2sConfigHand, sampleBuf, sizeof(sampleBuf), &bytesWritten, portMAX_DELAY);
    }
}

// Inicializa el sintetizador
void initSynth(uint8_t bck = 26, uint8_t ws = 25, uint8_t data = 22) {
    // Configura el canal I2S
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .bclk = (gpio_num_t)bck, .ws = (gpio_num_t)ws, .dout = (gpio_num_t)data }
    };
    i2s_new_channel(&chan_cfg, &_i2sConfigHand, NULL);
    i2s_channel_init_std_mode(_i2sConfigHand, &std_cfg);
    i2s_channel_enable(_i2sConfigHand);

    // Inicializa la tabla de seno y la tarea de síntesis
    initSineTable();
    xTaskCreatePinnedToCore(synthProcess, "TaskSynth", 4096, NULL, ESP_TASK_PRIO_MAX - 1, &_synthTaskHandle, 0);
}

// Pausa el sintetizador
void pauseSynth() {
    vTaskSuspend(_synthTaskHandle);
    Serial.println("Sintetizador pausado");
}

// Reanuda el sintetizador
void resumeSynth() {
    vTaskResume(_synthTaskHandle);
    Serial.println("Sintetizador reanudado");
}