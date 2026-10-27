# KytyPS5

[![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20Linux%20x64-0078D4.svg)](#-requisitos-de-sistema-recomendados)
[![Status](https://img.shields.io/badge/status-yotei--bringup-brightgreen.svg)](#-guia-completo-de-execução)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)

> [!IMPORTANT]
> **Branch `yotei-bringup`:** Esta branch concentra as correções, otimizações e diagnósticos específicos para a execução de **Ghost of Yōtei (PPSA26344)** no PC (testado com processadores AMD Ryzen e GPUs NVIDIA GeForce RTX 3070 / 4090). Para detalhes aprofundados dos switches internos, consulte também [docs/yotei-fork.md](docs/yotei-fork.md).

---

## 📑 Índice
1. [Requisitos de Sistema e Ferramentas](#-requisitos-de-sistema-e-ferramentas)
2. [Passo a Passo de Compilação (Build from Source)](#-passo-a-passo-de-compilação-build-from-source)
3. [Guia Completo de Execução](#-guia-completo-de-execução)
4. [Detalhamento de Cada Parte da Configuração](#-detalhamento-de-cada-parte-da-configuração)
5. [Solução de Problemas (Troubleshooting)](#-solução-de-problemas-troubleshooting)

---

## 💻 Requisitos de Sistema e Ferramentas

### Hardware Recomendado
* **Processador**: AMD Ryzen 5 5600 / Intel Core i5-12400 (6 núcleos / 12 threads) ou superior.
* **Memória RAM**: 16 GB a 32 GB DDR4 / DDR5.
* **Placa de Vídeo**: NVIDIA GeForce RTX 3070 (8 GB VRAM) / RTX 4070 ou superior com suporte a Vulkan 1.3+.
* **Armazenamento**: SSD NVMe (necessário para streaming rápido de texturas e malhas).
* **Sistema Operacional**: Windows 11 x64 (Build 22H2 ou superior).

### Ferramentas de Compilação Necessárias (Windows)
1. **Visual Studio 2022** (Community ou Build Tools) com a carga *"Desenvolvimento para Desktop com C++"* e o Windows 11 SDK instalados.
2. **LLVM Clang (`clang-cl`)**: versão 18 ou superior ([Download LLVM](https://github.com/llvm/llvm-project/releases)).
3. **CMake**: versão 3.25 ou superior.
4. **Ninja Build**: executável `ninja.exe` acessível pelo PATH (incluso em MinGW/WinLibs ou no Visual Studio).
5. **Vulkan SDK**: versão 1.3.x ou 1.4.x instalada ([LunarG Vulkan SDK](https://vulkan.lunarg.com/)).
6. **Python**: versão 3.10 ou superior instalada e adicionada ao PATH.

---

## 🛠️ Passo a Passo de Compilação (Build from Source)

Siga este procedimento caso você esteja clonando o repositório do zero ou queira recompilar o emulador após efetuar modificações no código-fonte.

### 1. Clonar o Repositório com Submódulos
```powershell
git clone --recursive -b yotei-bringup https://github.com/andsouzam/KytyPS5.git
cd KytyPS5
```

### 2. Configurar o Projeto com CMake (Geração dos Arquivos de Build)
Abra um terminal (PowerShell ou Prompt de Comando do Desenvolvedor) e execute a configuração inicial apontando para o compilador `clang-cl` e o gerador `Ninja`:

```powershell
cmake -B _Build/windows-custom -G "Ninja" `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER="clang-cl" `
  -DCMAKE_CXX_COMPILER="clang-cl" `
  -DKYTY_BUILD_LAUNCHER=OFF `
  -DKYTY_WINDOWS_HOST_ASLR=ON `
  -DKYTY_SHADER_CACHE_KEY=aaeb0f01f465c27db0fbd0ab9b1a10b14748c69242cb1cce102d130a28aeba4b
```
> [!NOTE]
> A flag `-DKYTY_SHADER_CACHE_KEY=...` fixa a chave de cache para garantir que os shaders pré-compilados salvos em disco (`.bin`) permaneçam 100% válidos entre compilações.

### 3. Compilar o Executável
Execute o Ninja para compilar o executável `kyty_emulator`:

```powershell
cmd.exe /c "set PATH=C:\winlibs\mingw64\bin;%PATH% && ninja -C _Build\windows-custom kyty_emulator"
```
O executável compilado será gerado em:
`C:\Users\Anderson\KytyPS5\_Build\windows-custom\kyty_emulator.exe`

### 4. (Opcional) Copiar para a Pasta Dedicada de Runtime
Caso utilize uma pasta dedicada de execução no drive `F:`:
```powershell
Copy-Item "_Build\windows-custom\kyty_emulator.exe" "F:\KytyPS5-Windows-x64\kyty_emulator.exe" -Force
```

---

## 🎮 Guia Completo de Execução

### 1. Entendendo a Estrutura de Arquivos

| Categoria | Arquivo / Diretório | Descrição |
| --- | --- | --- |
| **Projeto (Git)** | [`scripts/run_yotei.bat`](scripts/run_yotei.bat) | Script que inicia o jogo, configurando automaticamente ambiente, flags e caminhos. |
| **Projeto (Git)** | [`scripts/precompile_yotei_shaders.bat`](scripts/precompile_yotei_shaders.bat) | Script para pré-compilar a biblioteca de shaders offline antes da partida. |
| **Projeto (Git)** | [`scripts/kyty_settings.ini`](scripts/kyty_settings.ini) | Presets de sincronização, limites de buffer e DRS. |
| **Compilado** | `_Build\windows-custom\kyty_emulator.exe` | Executável compilado pelo Ninja. |
| **Dados (Local)** | `F:\GOI\PPSA26344-app\` | Dump legítimo dos arquivos do jogo Ghost of Yōtei (~60 GB). *(Não incluso no Git)*. |
| **Dados (Local)** | `_PipelineCache\PPSA26344.bin` | Cache Vulkan binário da placa de vídeo (~1,65 GB). *(Não incluso no Git)*. |
| **Dados (Local)** | `_PipelineCache\PPSA26344.v9.shaders` | Journal de shaders extraídos para pré-compilação (~22,5 MB). |
| **Dados (Local)** | `_SaveData\` | Saves e progresso de jogo do usuário. |

---

### 2. Executando o Jogo

#### Passo A: Pré-compilar os Shaders (Recomendado)
A pré-compilação aquece o driver Vulkan da GPU com os pipelines salvos no journal, eliminando engasgos (*stutters*) severos e congelamentos na abertura de mundo:
```cmd
scripts\precompile_yotei_shaders.bat
```

#### Passo B: Iniciar o Jogo
Você pode executar o jogo de duas formas equivalentes:

* **Opção 1 (Direto pelo repositório)**:
  ```cmd
  scripts\run_yotei.bat
  ```
  *(O script localiza automaticamente o executável em `_Build\windows-custom` ou em `F:\KytyPS5-Windows-x64`).*

* **Opção 2 (Pelo diretório de instalação do drive F:)**:
  ```cmd
  F:\KytyPS5-Windows-x64\run_yotei.bat
  ```

#### Customização de Caminho do Jogo
Se os arquivos do jogo estiverem em outra pasta ou unidade, basta definir a variável `YOTEI_GAME_PATH` antes da chamada:
```cmd
set YOTEI_GAME_PATH=D:\Jogos\PPSA26344-app
scripts\run_yotei.bat
```

---

## 🔍 Detalhamento de Cada Parte da Configuração

O comando executado internamente por `run_yotei.bat` combina variáveis de runtime do emulador e argumentos de linha de comando:

### 1. Variáveis de Ambiente do Runtime (`set KYTY_*`)

| Variável | Valor | Finalidade |
| --- | :---: | --- |
| `KYTY_COMPUTE_CHAIN` | `1` | Encadeia dispatches consecutivos de computação sem barreiras redundantes no host Vulkan, diminuindo o overhead de CPU. |
| `KYTY_READBACK_QUEUE` | `0` | Desativa a fila genérica de readback lento, garantindo publicação imediata de dados essenciais para o guest. |
| `KYTY_READBACK_TARGETED_PUBLICATION` | `1` | Publica leituras de buffers específicos de forma direcionada, eliminando esperas globais e travamentos de GPU. |
| `KYTY_GPU_PALETTE_SANITIZE` | `0` | Desativa a sanitização forçada na GPU sobre buffers de ossos, preservando a malha anatômica da protagonista (Atsu) sem deformações de vértices. |
| `KYTY_BINDLESS_KEY_BOUNDS` | `1` | Carrega apenas as entradas de textura bindless alcançáveis pelo shader em execução, prevenindo crash de `DEVICE_LOST` no Torii. |
| `KYTY_BINDLESS_EAGER` | `1` | Carrega tabelas de texturas de compute raros imediatamente no mesmo quadro, evitando artefatos pretos. |
| `KYTY_BINDLESS_TSHARP_CHECK` | `1` | Valida registradores de textura `T#` antes de enviar à Vulkan, ignorando dados corrompidos de buffers liberados. |
| `KYTY_AVPLAYER_RECYCLE` | `1` | Recicla e reutiliza buffers do decodificador de vídeo (`AvPlayer`), prevenindo estouro de memória nas cutscenes. |
| `KYTY_PRESENT_WHEN_READY` | `1` | Apresenta o quadro renderizado assim que a fila gráfica termina, eliminando drops artificiais de frames. |

---

### 2. Argumentos de Linha de Comando (`kyty_emulator.exe`)

```cmd
kyty_emulator.exe ^
  --game "F:\GOI\PPSA26344-app" ^
  --screen-width 1920 --screen-height 1080 ^
  --game-preset-priority Performance ^
  --gpu-frames-ahead 0 ^
  --gpu-timestamp-scale 200 ^
  --label-flush-interval-us 2000 ^
  --async-pipelines false ^
  --defer-dispatches false ^
  --relaxed-readback false ^
  --speculative-draws true ^
  --record-thread true ^
  --amd-cpu
```

* **`--game "F:\GOI\PPSA26344-app"`**  
  Caminho absoluto ou relativo para o diretório raiz do dump do jogo PS5.
* **`--screen-width 1920 --screen-height 1080`**  
  Define a resolução da janela e do render target de saída para 1080p (Full HD), mantendo a alocação de VRAM sob controle em placas de 8 GB (RTX 3070).
* **`--game-preset-priority Performance`**  
  Força as configurações internas da engine do jogo a priorizarem taxa de quadros (*Performance Mode*).
* **`--gpu-frames-ahead 0`**  
  Sincroniza o processador gráfico da CPU diretamente com os pontos de suspensão da GPU via semáforo atômico, eliminando atrasos e sobrescrita de comandos em ring-buffer.
* **`--gpu-timestamp-scale 200`**  
  Escala os timestamps de GPU reportados para a engine. Faz o Dynamic Resolution Scaling (DRS) entender que a GPU precisa de margem livre, estabilizando os quadros sem quedas bruscas de resolução.
* **`--label-flush-interval-us 2000`**  
  Intervalo em microssegundos (2 ms) para descarga periódica dos labels de sincronização da GPU.
* **`--async-pipelines false`**  
  Força a compilação síncrona determinística de variantes gráficas essenciais para evitar artefatos ou elementos invisíveis.
* **`--defer-dispatches false`**  
  Execução imediata de comandos de computação para manter sincronismo de física e animação.
* **`--relaxed-readback false`**  
  Garante coerência estrita entre as leituras de memória da CPU e escritas da GPU.
* **`--speculative-draws true`**  
  Alimenta chamadas de desenho de forma especulativa para mitigar bolhas de ociosidade no pipeline gráfico.
* **`--record-thread true`**  
  Transfere a gravação e parsing de comandos PM4 para uma thread de suporte dedicada, liberando a thread principal de renderização.
* **`--amd-cpu`**  
  Aplica otimizações para processadores AMD Ryzen (Zen 3 / 5600), otimizando o patch de instruções de CPU guest para x86-64 do host.

---

## ❓ Solução de Problemas (Troubleshooting)

* **Estouro de Pilha / Crash Silencioso ao Carregar Shaders**:  
  Certifique-se de estar utilizando a versão atualizada da branch `yotei-bringup` com o patch `ThreadWithStack` (64 MiB reservados para a GPU thread). No Windows, threads padrão com apenas 1 MiB sofrem overflow ao processar a árvore de shaders do RDNA2.
* **Vértices Explodindo na Personagem (Atsu)**:  
  Verifique se a variável `KYTY_GPU_PALETTE_SANITIZE=0` está ativa. A sanitização agressiva antiga corrompia matrizes de pele (*skinning*) legítimas.
* **Quedas Bruscas de FPS ou Travamento em Telas Pretas**:  
  Execute sempre o script `scripts\precompile_yotei_shaders.bat` antes de rodar o jogo para garantir que o cache `_PipelineCache\PPSA26344.bin` esteja preenchido.
* **Falta de Memória VRAM (Out of Device Memory)**:  
  Mantenha a resolução em 1080p (`--screen-width 1920 --screen-height 1080`). Resoluções 4K excedem facilmente o limite de 8 GB da RTX 3070.


---

## 🤖 Build Automático com GitHub Actions (CI/CD)

O projeto possui o workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) configurado para compilar, testar e empacotar automaticamente o emulador na nuvem:

* **Disparo Automático (`push` / `pull_request`)**:  
  Todo `git push` para a branch `yotei-bringup` (ou `main`) dispara a compilação em paralelo para **Windows x64** (`clang-cl` + LTO + `sccache`), **Linux x64** e **macOS x86_64**, executa a suíte de testes (`ctest`, incluindo `host_thread` e `bone_palette_staging_policy`) e publica o pacote completo `KytyPS5-Windows-x64` (já com `kyty_emulator.exe`, `launcher.exe`, DLLs do Qt6 e os scripts `run_yotei.bat`, `precompile_yotei_shaders.bat` e `kyty_settings.ini`) na aba **Actions -> Artifacts**.
* **Compatibilidade de Cache de Shaders**:  
  O workflow fixa automaticamente a `KYTY_SHADER_CACHE_KEY`, garantindo que qualquer executável baixado do GitHub Actions funcione diretamente com o seu `_PipelineCache\PPSA26344.bin` local sem precisar recompilar shaders do zero.
* **Geração de Release Oficial (`workflow_dispatch`)**:  
  Acessando **Actions -> Build and Release KytyPS5 (Yotei) -> Run workflow** no GitHub, além de gerar os artefatos, o workflow cria automaticamente uma **GitHub Release** com os arquivos `.zip` / `.tar.gz` prontos para download.
