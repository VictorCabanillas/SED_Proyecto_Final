# SED_Proyecto_Final
## Requisitos

- ESP-IDF v5.x instalado y configurado
- Python 3.8+
- Placa ESP32 compatible


Este repositorio contiene el proyecto final para la asignatura de Sistemas Empotrados Digitales. En la carpeta `main` se incluyen dos archivos fuente: `app_main_panel.c` y `app_main_sensor.c`, correspondientes a la lógica de funcionamiento de dos placas ESP32 distintas (panel y sensor).

## Instrucciones para Compilar

1. **Modificar rutas locales**  
    Ajustar las rutas en los siguientes archivos según la ubicación del proyecto en tu ordenador:
    - `dependencies.lock`
    - `.vscode/settings.json`
    - `main/idf_component.yml`

2. **Seleccionar el archivo principal**  
    Antes de compilar, mover fuera de la carpeta `main` el archivo que no se utilizará. Solo debe quedar uno de los siguientes:
    - `app_main_panel.c`  
    - `app_main_sensor.c`

3. **Generar llaves para OTA**  
    Crear las llaves necesarias para la actualización OTA en la carpeta `keys`.

## Conexión / Pines utilizados

| Nombre del pin      | Número de GPIO |
|---------------------|:--------------:|
| LED                 |      17        |
| Buzzer              |      4         |
| RFID MISO           |      19        |
| RFID MOSI           |      23        |
| RFID SCLK           |      18        |
| RFID SDA            |      5         |
| Sensor Presencia    |      35        |

*Usar 3.3V*

## **Autores**

```markdown
- Víctor Cabanillas Solís - [@VictorCabanillas](https://github.com/VictorCabanillas)
- Gonzalo Isla Llave - [@gonzisll](https://github.com/gonzisll)