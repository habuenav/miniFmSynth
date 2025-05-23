# miniFmSynth

**miniFmSynth** es una librería de síntesis de audio FM (Frecuencia Modulada) diseñada para microcontroladores ESP32. Permite generar sonidos en tiempo real con hasta 8 voces de polifonía, utilizando envolventes ADSR y una tabla de instrumentos predefinida. Es ideal para proyectos de música interactiva, sintetizadores DIY, instalaciones artísticas y más.

## Características

- **Síntesis FM**: Genera sonidos mediante modulación de frecuencia con portadora y modulador.
- **Polifonía**: Soporta hasta 8 voces simultáneas (configurable).
- **Envolventes ADSR**: Controla la dinámica del sonido con ataque, decaimiento, sostenimiento y liberación.
- **Instrumentos predefinidos**: Incluye 12 instrumentos (piano, xilófono, guitarra, etc.) con parámetros personalizados.
- **Salida I2S**: Compatible con DACs externos (como PCM5102) para audio de alta calidad.
- **Funciones de control**: Soporta activación/desactivación de notas, cambio de volumen, ajuste de tono y selección de instrumentos por canal.
- **Optimizado para ESP32**: Utiliza la API I2S estándar y tareas FreeRTOS para un rendimiento eficiente.

## Requisitos

- **Hardware**:
  - Microcontrolador ESP32 (ESP32-WROOM, ESP32-S3, etc.).
  - DAC externo (opcional, como PCM5102) para mejorar la calidad de audio.
- **Software**:
  - Arduino IDE o PlatformIO con el framework Arduino-ESP32.
  - Biblioteca `driver/i2s_std.h` (incluida en el framework ESP32).
- **Dependencias**: Ninguna adicional, solo la biblioteca estándar de Arduino y ESP32.

## Instalación

1. **Descarga la librería**:
   - Clona este repositorio o descárgalo como ZIP desde GitHub:
     ```bash
     git clone https://github.com/usuario/miniFmSynth.git
     ```
2. **Instala en Arduino IDE**:
   - Copia la carpeta `miniFmSynth` al directorio `libraries` de tu Arduino IDE (usualmente en `~/Arduino/libraries`).
   - Reinicia el IDE para que reconozca la librería.
3. **Configura tu proyecto**:
   - Incluye la librería en tu sketch con `#include <miniFmSynth.h>` (asegúrate de renombrar el archivo `.ino` a `.h` si creas una librería formal).
   - Configura los pines I2S según tu hardware (por defecto: BCLK=26, WS=25, DOUT=22).

## Uso

La librería proporciona funciones para inicializar el sintetizador, asignar instrumentos, controlar notas y ajustar parámetros de audio.

### Ejemplo básico

```cpp
#include <miniFmSynth.h>

void setup() {
  Serial.begin(115200);
  initSynth(); // Inicializa el sintetizador con pines I2S por defecto
  setMaxNotes(8); // Establece el máximo de voces
  setVolume(80); // Ajusta el volumen global al 80%
  setInstrument(0, 0); // Asigna el instrumento "Piano" al canal 0
}

void loop() {
  noteOn(0, 60, 127); // Toca la nota C4 (MIDI 60) en el canal 0 con máxima velocity
  delay(1000);
  noteOff(0, 60); // Apaga la nota
  delay(1000);
}
```

### Funciones principales

- `initSynth(uint8_t bck=26, uint8_t ws=25, uint8_t data=22)`: Inicializa el sintetizador con los pines I2S especificados.
- `setMaxNotes(uint8_t maxNotas)`: Establece el número máximo de voces (máximo 8).
- `setVolume(uint8_t vol)`: Ajusta el volumen global (0-100).
- `setInstrument(uint8_t channel, uint8_t instrument)`: Asigna un instrumento a un canal.
- `noteOn(uint8_t channel, uint8_t note, uint8_t velocity)`: Activa una nota en un canal con la velocity especificada.
- `noteOff(uint8_t channel, uint8_t note)`: Apaga una nota en un canal.
- `allNoteOff()`: Apaga todas las notas activas.
- `alterVolNote(uint8_t channel, uint8_t velocity)`: Modifica el volumen de las notas en un canal.
- `alterPitchNote(uint8_t channel, uint8_t cant)`: Ajusta el tono de las notas en un canal.
- `pauseSynth()`: Pausa la generación de audio.
- `resumeSynth()`: Reanuda la generación de audio.

### Ejemplo avanzado

```cpp
#include <miniFmSynth.h>

void setup() {
  Serial.begin(115200);
  initSynth(26, 25, 22); // Inicializa I2S con pines personalizados
  setVolume(90); // Volumen al 90%
  setInstrument(0, 0); // Piano en canal 0
  setInstrument(1, 2); // Guitarra en canal 1
}

void loop() {
  // Toca una secuencia de notas
  noteOn(0, 60, 100); // C4 en canal 0 (Piano)
  delay(500);
  noteOn(1, 64, 100); // E4 en canal 1 (Guitarra)
  delay(500);
  alterVolNote(0, 50); // Reduce el volumen del canal 0
  alterPitchNote(1, 64); // Ajusta el tono del canal 1
  delay(1000);
  allNoteOff(); // Apaga todas las notas
  delay(1000);
}
```

## Notas de implementación

- **Calidad de audio**: Para obtener la mejor calidad, conecta un DAC externo (como PCM5102) a los pines I2S. El DAC interno del ESP32 es limitado (8 bits).
- **Rendimiento**: La librería está optimizada con `fast-math` y ejecuta la síntesis en una tarea FreeRTOS de alta prioridad.
- **Limitaciones**: El número máximo de voces (`MAX_VOICES`) es fijo en 8. Modificar dinámicamente requeriría cambios en la estructura `voices[]`.
- **Personalización**: Puedes ajustar los parámetros de los instrumentos en la tabla `instruments[]` para crear nuevos sonidos.

## Estructura del repositorio

```
miniFmSynth/
├── miniFmSynth.ino  # Código fuente principal
├── README.md        # Este archivo
├── examples/        # Ejemplos de uso
│   ├── BasicSynth.ino
│   ├── AdvancedSynth.ino
└── LICENSE          # Licencia del proyecto
```

## Licencia

Este proyecto está licenciado bajo la **MIT License**. Consulta el archivo `LICENSE` para más detalles.

## Contribuciones

¡Las contribuciones son bienvenidas! Si deseas mejorar la librería, añadir nuevos instrumentos o optimizar el código:
1. Haz un fork del repositorio.
2. Crea una rama para tus cambios (`git checkout -b feature/nueva-funcion`).
3. Realiza un pull request con una descripción clara de los cambios.

