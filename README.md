# NoctaPrivacy v0.1.2 — capa de desenfoque permanente para Streamlabs Desktop

![description](https://raw.githubusercontent.com/Strix-Z/NoctaPrivacy/ac46b58403c45a15ea034dbdfc862d088f165a2a/Example.png)

**Estado: prototipo beta.** La versión anterior compiló en GitHub Actions; los cambios de v0.1.2 requieren una nueva compilación y pruebas en tu PC. **No confíes en un blur como único método de ocultar datos privados durante un directo.**

## ¿Lee la memoria de PKA?

No: el código solo enumera títulos de ventanas mediante `EnumWindows` y `GetWindowTextW`, selecciona el identificador `HWND` y pide a Windows.Graphics.Capture los fotogramas visuales de esa ventana. **No llama a `OpenProcess`, `ReadProcessMemory`, ni `WriteProcessMemory`; no inyecta DLL, no lee archivos de PKA, no captura teclas ni envía entradas al juego; tampoco se conecta a servidores del juego o a Internet mientras se ejecuta.** La descarga de Spout2 sí requiere conexión *únicamente durante la compilación*. Esto no garantiza que un anticheat permita toda captura externa.

## Requisitos

- Windows 10 actualizado (1903+) / Windows 11, x64, modo SDR.
- Visual Studio 2022 con **Desktop development with C++** y **Windows 10/11 SDK**.
- Git for Windows, CMake 3.21+.
- GPU D3D11 (NVIDIA RTX 3080 soporta D3D11).
- Streamlabs Desktop con fuente **Spout2 Capture** (receptor incorporado).
- GitHub disponible durante el primer build para descargar Spout2 (tag verificado `2.007.017`).

## Compilar gratis desde GitHub Actions (sin Visual Studio en tu PC)

1. En GitHub crea un repositorio nuevo **privado** con nombre `NoctaPrivacy` y rama predeterminada `main`.
2. Sube **el contenido de esta carpeta** a la raíz del repositorio. Comprueba que GitHub muestre el archivo `.github/workflows/windows-build.yml` en esa ruta exacta; si quedó dentro de una carpeta `NoctaPrivacy/` adicional, el workflow no funcionará.
3. Abre el repositorio → **Actions** → **Build NoctaPrivacy (Windows x64)** → **Run workflow** → **Run workflow**. También se ejecuta automáticamente al enviar cambios a `main`.
4. Espera a que termine el run. Si está verde, entra al run y descarga **NoctaPrivacy-windows-x64** desde **Artifacts**.
5. Descomprime el artifact en tu PC: incluirá `NoctaPrivacy.exe`, instrucciones y la licencia del componente Spout2.
6. Abre PKA en modo ventana o borderless, ejecuta el EXE en PowerShell con `./NoctaPrivacy.exe "PokeAlliance"`, y en Streamlabs agrega **Spout2 Capture** con sender **NoctaPrivacy**.

**Estado de validación:** la versión anterior compiló correctamente con MSVC en GitHub Actions. Cada cambio nuevo requiere compilarse de nuevo. Que GitHub genere el ejecutable **no** demuestra compatibilidad funcional ni protección fiable de datos privados en un directo.

> GitHub Actions usa Windows virtual de GitHub para compilar; el EXE después se ejecuta **localmente en tu PC** y captura la ventana de PKA local. GitHub no recibe el video del juego por este procedimiento.

## Compilar

1. Extrae `NoctaPrivacy` a una carpeta de tu PC.
2. En Inicio abre **Developer PowerShell for VS 2022**.
3. Accede a la carpeta del proyecto y ejecuta:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

4. Si compila sin errores, el ejecutable se encontrará en:

```text
build\Release\NoctaPrivacy.exe
```

Como alternativa manual:

```powershell
git clone --depth 1 --branch 2.007.017 https://github.com/leadedge/Spout2.git external/Spout2
cmake -S . -B build -A x64 -G "Visual Studio 17 2022"
cmake --build build --config Release --target NoctaPrivacy
```

## Uso: fuente de desenfoque siempre activa

**Diseño deseado:** la captura original de PKA permanece VISIBLE en Streamlabs, con una fuente `Spout2 Capture` llamada `NoctaPrivacy` colocada JUSTO ENCIMA. NoctaPrivacy procesa fotogramas y publica una imagen ya desenfocada permanentemente, incluso cuando la fuente está oculta en Streamlabs. Activar o desactivar el efecto equivale únicamente a mostrar/ocultar la fuente Spout2, **sin filtros ni atajos del ejecutable para cambiar el blur**.

Esto **sigue usando Windows.Graphics.Capture sobre una ventana de PKA** (título de ventana y `HWND`), aunque NO abre el proceso, lee memoria privada ni inyecta código. Si se requiere *cero consultas a la ventana del juego*, esta implementación no cumple esa condición.

## Ejecutar y conectar

1. Abre PKA primero y usa modo ventana o *borderless*. El título exacto varía: si contiene PokeAlliance, ejecuta:

```powershell
.\build\Release\NoctaPrivacy.exe "PokeAlliance"
```

   Si no reconoce la ventana, prueba con una parte del título que veas en la barra de PKA, entre comillas. La búsqueda no diferencia mayúsculas.
2. Abre Streamlabs Desktop → **Sources → + → Spout2 Capture**.
3. Elige `NoctaPrivacy` como sender y **Opaque** como composite mode.
4. En el panel de fuentes de Streamlabs deja **Game Capture (PKA)** ABAJO y **Spout2 Capture (NoctaPrivacy)** ARRIBA. No ocultes ni elimines Game Capture.
5. Ajusta el tamaño, recorte y posición de Spout2 Capture para cubrir exactamente la región del juego en el lienzo: la captura de ventana de Windows puede incluir bordes diferentes a Game Capture. Verifica con textos/contornos para que no queden franjas sin desenfocar.
6. Oculta Spout2 Capture mediante el icono del ojo: se verá PKA normal. Muéstrala de nuevo y verás PKA desenfocado. No necesitas duplicar escenas ni añadir filtros a Streamlabs.
7. Para una tecla, entra en **Settings → Hotkeys**, localiza la fuente `NoctaPrivacy` en la escena y asigna Mostrar/Ocultar (por ejemplo `Ctrl+Alt+F10`), siempre que no choque con otro atajo.
8. No inicies directo hasta probar la superposición en una grabación local. **Si NoctaPrivacy se cierra o Spout falla mientras la capa debería ocultar algo, la captura original puede quedar visible.** Usa además una fuente de color opaco independiente para privacidad sensible.

## Atajos

| Atajo | Acción |
|---|---|
| `Ctrl+Alt+F10` | Configurable en **Streamlabs**, para Mostrar/Ocultar la fuente `NoctaPrivacy` (ya no lo utiliza el EXE) |
| `Ctrl+Alt+F11` | Alternar pantalla opaca negra (emergencia) |
| `Ctrl+Alt+F12` | Cerrar NoctaPrivacy |

El desenfoque se aplica **constantemente** a la señal Spout2; la intensidad por defecto es 7.0, ajustable editando `float strength = 7.0f;` en `src/main.cpp` y recompilando. No todos los textos quedarán irreconocibles: la pantalla opaca es la única opción segura para contraseñas y datos confidenciales.

## Funcionamiento y limitaciones

- Ruta de imagen: `Ventana de PKA → Windows Graphics Capture → textura D3D11 → shader separable blur → Spout2 → Streamlabs`.
- Sin filtros de Streamlabs ni *Paste Reference*: no hay filtros que se compartan con Game Capture.
- GPU-only de captura a envío, sin descargar imágenes a CPU.
- Rendimiento estimado, **no medido**; se envía un frame ~cada 33 ms (máximo cercano a 30 FPS, sin garantía de sincronización).
- Si no llegan fotogramas recientes durante >1 segundo, la aplicación intenta emitir una textura negra. Esto no es una garantía absoluta de privacidad: **si el proceso se cierra/crashea o falla Spout, Streamlabs podría congelar el último fotograma o recurrir a otra fuente**. Mantén un overlay opaco independiente de emergencia en Streamlabs.
- El refresco de hotkeys y el procesamiento no son atómicos con la salida del streaming: algunos fotogramas pueden emitirse antes del blackout al presionar la tecla.
- El programa no captura audio. Mantén la captura de audio que ya utilizas en Streamlabs.
- Windows Graphics Capture puede no capturar ventanas minimizadas, contenido protegido, juegos exclusivos fullscreen ni algunos anti-cheats. Prueba en modo sin bordes.
- Si hay HDR o varias GPUs/adaptadores diferentes podría fallar. Configura PKA, NoctaPrivacy y Streamlabs en la misma GPU NVIDIA; usa SDR.
- Las dimensiones del remitente Spout cambian si redimensionas la ventana; puede requerir actualizar/recargar la fuente en Streamlabs.
- El software no comprueba si Streamlabs está mostrando el sender, ni puede garantizar que no exista una segunda captura original visible.

## Validación mínima antes de producción

1. Prueba fuente Spout y captura visual.
2. Graba en Streamlabs 60 segundos y muestra/oculta la fuente Spout2 varias veces. La imagen de Spout2 debe estar permanentemente borrosa, sin necesidad de enviar comandos a NoctaPrivacy.
3. Prueba F11 con un texto grande: nunca debe aparecer bajo el rectángulo negro.
4. Minimiza PKA, cierra PKA, cambia resolución y deja el programa sin fotogramas. Verifica qué aparece en una grabación.
5. Cierra NoctaPrivacy y verifica si Streamlabs congela la última imagen. Si ocurre, no dependas del sender como barrera final; activa una fuente opaca independiente.
6. Verifica caída de FPS de PKA/Streamlabs, consumo GPU y comportamiento con *borderless*.

## Dependencias y licencias

El proyecto propio es código de ejemplo sin garantía. Spout2 se descarga desde su repositorio original y conserva su licencia separada: https://github.com/leadedge/Spout2. Windows.Graphics.Capture está documentado por Microsoft: https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture.
